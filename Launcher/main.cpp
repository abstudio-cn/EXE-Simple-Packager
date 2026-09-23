// ESPLauncher — Python 应用启动器（随包携带解释器）
//
// 设计要点：
//  * 直接 LoadLibrary(python3XX.dll) + Py_Main(...)：解释器在**本进程内**运行，
//    不产生任何子进程 → 任务管理器看到的进程名就是应用名（<AppName>.exe），
//    卸载器按进程名结束进程既准确又安全（不会误杀其它 python.exe）。
//  * 命令行：无参数 → 运行 launch.ini 配置的入口脚本（可带附加参数）；
//    有参数 → 原样透传给解释器（等价 python.exe，multiprocessing / -c / -m 均可正常工作）。
//  * launch.ini 与本 exe 同目录：
//      [Python]
//      Dll=python313.dll       ; 相对本目录或绝对路径
//      Script=app\main.py      ; 无参数启动时的入口脚本
//      Args=                   ; 追加给入口脚本的参数
//      Home=                   ; 可选：Python 根目录（默认本 exe 目录）
//  * 控制台版 / 窗口版由链接子系统区分（CMakeLists 中两个 target）。
//  * 本文件不含 Qt，成品约 100 KB，随包携带代价可忽略。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <vector>

typedef int(__cdecl *PyMainFn)(int, wchar_t **);

namespace {

std::wstring exePath()
{
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    return std::wstring(buf, n);
}

std::wstring exeDir()
{
    const std::wstring p = exePath();
    const size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : p.substr(0, pos);
}

std::wstring readIni(const std::wstring &ini, const wchar_t *key)
{
    wchar_t buf[8192] = {0};
    GetPrivateProfileStringW(L"Python", key, L"", buf, 8192, ini.c_str());
    return std::wstring(buf);
}

void splitArgs(const std::wstring &s, std::vector<std::wstring> *out)
{
    std::wstring cur;
    bool inQuote = false;
    for (const wchar_t c : s) {
        if (c == L'"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && (c == L' ' || c == L'\t')) {
            if (!cur.empty()) {
                out->push_back(cur);
                cur.clear();
            }
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty())
        out->push_back(cur);
}

// 目录内优先选 python3XX.dll（带小版本号），其次 python3.dll（稳定 ABI 转发层）
std::wstring findRuntimeDll(const std::wstring &dir)
{
    WIN32_FIND_DATAW fd;
    const std::wstring pattern = dir + L"\\python3*.dll";
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    std::wstring versioned;
    std::wstring stable;
    do {
        std::wstring name(fd.cFileName);
        if (name.size() < 5 || name.compare(name.size() - 4, 4, L".dll") != 0)
            continue;
        // python3.dll 是稳定 ABI 转发层；python313.dll 才是完整运行库
        const bool hasMinor = name.size() > 8 && iswdigit(name[7]);
        if (hasMinor) {
            versioned = name;
            break;
        }
        if (stable.empty())
            stable = name;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return versioned.empty() ? stable : versioned;
}

void writeLog(const std::wstring &msg)
{
    wchar_t tmp[MAX_PATH] = {0};
    if (!GetTempPathW(MAX_PATH, tmp))
        return;
    const std::wstring log = std::wstring(tmp) + L"ESPLauncher.log";
    FILE *f = nullptr;
    if (_wfopen_s(&f, log.c_str(), L"a, ccs=UTF-8") != 0 || !f)
        return;
    fwprintf(f, L"%s\n", msg.c_str());
    fclose(f);
}

void fail(const std::wstring &msg)
{
    writeLog(L"[错误] " + msg);
    fwprintf(stderr, L"[ESPLauncher] %ls\n", msg.c_str());
    MessageBoxW(nullptr, msg.c_str(), L"启动失败", MB_ICONERROR | MB_OK);
}

int runApp(int argc, wchar_t **argv, bool console)
{
    const std::wstring dir = exeDir();
    const std::wstring ini = dir + L"\\launch.ini";

    std::wstring dll = readIni(ini, L"Dll");
    const std::wstring home = readIni(ini, L"Home");
    if (dll.empty())
        dll = findRuntimeDll(home.empty() ? dir : home);
    if (dll.empty()) {
        fail(L"未找到 Python 运行库（python3*.dll）。\n请确认安装包完整。");
        return 2;
    }

    std::wstring dllPath = dll;
    if (dllPath.find(L':') == std::wstring::npos && dllPath.find(L'\\') == std::wstring::npos)
        dllPath = (home.empty() ? dir : home) + L"\\" + dll;

    HMODULE h = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        // 兜底：忽略 launch.ini 里的 Dll，改用目录内任一的 python3*.dll
        const std::wstring alt = findRuntimeDll(dir);
        if (!alt.empty() && alt != dll) {
            dllPath = dir + L"\\" + alt;
            h = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
    }
    if (!h) {
        fail(L"无法加载 Python 运行库：\n" + dllPath + L"\n\n错误码：" + std::to_wstring(GetLastError()));
        return 2;
    }

    const PyMainFn fn = reinterpret_cast<PyMainFn>(GetProcAddress(h, "Py_Main"));
    if (!fn) {
        fail(L"Python 运行库未导出 Py_Main：\n" + dllPath);
        FreeLibrary(h);
        return 3;
    }

    // 以应用目录为工作目录（双击 exe 启动时当前目录可能是任意位置）
    SetCurrentDirectoryW(dir.c_str());

    std::vector<std::wstring> args;
    args.push_back(argv[0]);
    if (argc <= 1) {
        const std::wstring script = readIni(ini, L"Script");
        if (script.empty()) {
            fail(L"launch.ini 未配置入口脚本 (Script=)。");
            FreeLibrary(h);
            return 4;
        }
        args.push_back(script);
        std::vector<std::wstring> extra;
        splitArgs(readIni(ini, L"Args"), &extra);
        for (const std::wstring &a : extra)
            args.push_back(a);
    } else {
        // 等价 python.exe：-c / -m / 脚本路径等原样透传
        for (int i = 1; i < argc; ++i)
            args.push_back(argv[i]);
    }

    std::vector<wchar_t *> raw;
    raw.reserve(args.size() + 1);
    for (std::wstring &a : args)
        raw.push_back(const_cast<wchar_t *>(a.c_str()));
    raw.push_back(nullptr);

    if (!console) {
        // 窗口版：把标准流重定向到 NUL，避免解释器写 stdout 时出错
        FILE *nul = nullptr;
        _wfopen_s(&nul, L"NUL", L"w");
        if (nul) {
            *stdout = *nul;
            *stderr = *nul;
        }
    }

    const int rc = fn(int(raw.size()) - 1, raw.data());
    FreeLibrary(h);
    return rc;
}

} // namespace

#ifdef ESP_LAUNCHER_CONSOLE
int wmain(int argc, wchar_t **argv)
{
    return runApp(argc, argv, true);
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const int rc = runApp(argc, reinterpret_cast<wchar_t **>(argv), false);
    if (argv)
        LocalFree(argv);
    return rc;
}
#endif
