#include "UninstallRunner.h"

#include "IniFile.h"
#include "RegOps.h"
#include "ShellPin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUuid>

namespace esp {

namespace {
// i18n：静态库无 QObject 上下文，统一用 translate
QString tr_(const char *s)
{
    return QCoreApplication::translate("UninstallRunner", s);
}

QString commonProgramsDir()
{
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return {};
    return QString::fromWCharArray(buf);
}

QString commonDesktopDir()
{
    wchar_t buf[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_COMMON_DESKTOPDIRECTORY, nullptr, SHGFP_TYPE_CURRENT, buf) != S_OK)
        return {};
    return QString::fromWCharArray(buf);
}

QString trimPath(const QString &p)
{
    return QDir::cleanPath(p).toLower().replace('/', '\\');
}

bool samePath(const QString &a, const QString &b)
{
    return trimPath(a) == trimPath(b);
}
} // namespace

UninstallRunner::UninstallRunner(const QString &iniPath,
                                 const std::function<void(const QString &)> &log,
                                 const std::function<void(int percent)> &progress)
    : iniPath_(iniPath), log_(log), progress_(progress)
{
    selfExePath_ = QCoreApplication::applicationFilePath();

    IniFile ini(iniPath);
    appName_ = ini.get(QStringLiteral("AppInfo"), QStringLiteral("AppName"),
                       tr_("未知程序"));
    installDir_ = ini.get(QStringLiteral("Cleanup"), QStringLiteral("InstallDir"));
    if (installDir_.isEmpty())
        installDir_ = QCoreApplication::applicationDirPath();
    mainExe_ = ini.get(QStringLiteral("AppInfo"), QStringLiteral("MainExe"));
}

void UninstallRunner::run()
{
    auto setP = [this](int p) { if (progress_) progress_(p); };
    log_(tr_("开始卸载 %1 ...").arg(appName_));
    setP(3);

    const QString tempScript = prepareUninstallScript();
    setP(10);
    killMainProcess();
    setP(22);
    unpinFromTaskbar();
    setP(32);
    removeStartMenuFolder();
    setP(42);
    removeDesktopShortcut();
    setP(52);
    removeRegistryKey();
    setP(62);
    removeExtraPaths();
    setP(72);
    deleteInstallDirContents();
    setP(92);
    runUninstallScript(tempScript);

    log_(failures_ == 0
         ? tr_("卸载完成。")
         : tr_("卸载流程结束，有 %1 个步骤未成功（详见上方日志）。").arg(failures_));
    setP(100);
}

QString UninstallRunner::prepareUninstallScript()
{
    IniFile ini(iniPath_);
    const QString name = ini.get(QStringLiteral("Cleanup"), QStringLiteral("UninstallScript"));
    if (name.isEmpty())
        return {};

    const QString src = QDir(installDir_).filePath(name);
    if (!QFile::exists(src)) {
        log_(tr_("[警告] 未找到额外卸载命令文件：%1，跳过。").arg(src));
        return {};
    }
    const QString tmp = QDir::temp().filePath(
        QStringLiteral("uninst_%1%2").arg(QUuid::createUuid().toString(QUuid::Id128)).arg(QFileInfo(src).suffix().isEmpty() ? QString() : "." + QFileInfo(src).suffix()));
    if (QFile::copy(src, tmp)) {
        log_(tr_("已备份额外卸载命令到临时目录。"));
        return tmp;
    }
    failures_++;
    log_(tr_("[错误] 备份额外卸载命令失败。"));
    return {};
}

void UninstallRunner::runUninstallScript(const QString &tempScript)
{
    if (tempScript.isEmpty())
        return;
    log_(tr_("执行额外卸载命令 ..."));
    QProcess p;
    p.setWorkingDirectory(QDir::tempPath());
    p.start(tempScript, {}, QIODevice::ReadOnly);
    if (!p.waitForFinished(30000)) {
        failures_++;
        log_(tr_("[错误] 额外卸载命令执行超时。"));
    } else {
        log_(tr_("额外卸载命令执行完成，退出码 %1%2。")
                 .arg(p.exitCode()).arg(p.exitCode() == 0 ? QString() : tr_("（非 0）")));
        if (p.exitCode() != 0)
            failures_++;
    }
    QFile::remove(tempScript);
}

void UninstallRunner::killMainProcess()
{
    if (mainExe_.isEmpty())
        return;
    const QString procName = QFileInfo(mainExe_).completeBaseName();
    if (procName.isEmpty())
        return;

    QVector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                // 注意：szExeFile 是带扩展名的映像名（如 MyApp.exe），
                // 必须取 completeBaseName 再比较，否则永远匹配不到 → 运行中的程序不被结束。
                const QString imageName = QString::fromWCharArray(pe.szExeFile);
                if (QFileInfo(imageName).completeBaseName().compare(procName, Qt::CaseInsensitive) == 0)
                    pids.append(pe.th32ProcessID);
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    if (pids.isEmpty()) {
        log_(tr_("未发现正在运行的目标进程（%1）。").arg(procName));
        return;
    }
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!h) {
            failures_++;
            log_(tr_("[错误] 打开进程失败 (PID %1)。").arg(pid));
            continue;
        }
        if (TerminateProcess(h, 0)) {
            WaitForSingleObject(h, 3000);
            log_(tr_("已结束进程：%1 (PID %2)。").arg(procName).arg(pid));
        } else {
            failures_++;
            log_(tr_("[错误] 结束进程失败 (PID %1)。").arg(pid));
        }
        CloseHandle(h);
    }
}

void UninstallRunner::unpinFromTaskbar()
{
    IniFile ini(iniPath_);
    if (ini.get(QStringLiteral("Cleanup"), QStringLiteral("PinTaskbar")) != QLatin1String("1"))
        return;

    const QString folder = ini.get(QStringLiteral("Cleanup"), QStringLiteral("StartMenuFolder"));
    QString smLnk;
    if (!folder.isEmpty())
        smLnk = QDir(commonProgramsDir()).filePath(folder + QStringLiteral("/") + appName_ + QStringLiteral(".lnk"));
    const QString lnkInDir = QDir(installDir_).filePath(appName_ + QStringLiteral(".lnk"));
    const QString mainExePath = mainExe_.isEmpty() ? QString() : QDir(installDir_).filePath(mainExe_);

    QString target;
    if (QFile::exists(lnkInDir)) target = lnkInDir;
    else if (QFile::exists(smLnk)) target = smLnk;
    else if (QFile::exists(mainExePath)) target = mainExePath;

    if (target.isEmpty()) {
        log_(tr_("[警告] 未找到任务栏固定目标，跳过取消固定。"));
        return;
    }
    if (toggleTaskbarPin(target, false))
        log_(tr_("已取消任务栏固定。"));
    else
        log_(tr_("[警告] 未能自动取消任务栏固定，请手动右键取消。"));
}

void UninstallRunner::removeStartMenuFolder()
{
    IniFile ini(iniPath_);
    const QString folder = ini.get(QStringLiteral("Cleanup"), QStringLiteral("StartMenuFolder"));
    if (folder.isEmpty())
        return;
    const QString path = QDir(commonProgramsDir()).filePath(folder);
    if (!QDir(path).exists()) {
        log_(tr_("开始菜单文件夹不存在，跳过。"));
        return;
    }
    if (QDir(path).removeRecursively())
        log_(tr_("已删除开始菜单文件夹：%1").arg(path));
    else {
        failures_++;
        manualCleanup_.append(path);
        log_(tr_("[错误] 删除开始菜单文件夹失败：%1").arg(path));
    }
}

void UninstallRunner::removeDesktopShortcut()
{
    IniFile ini(iniPath_);
    if (ini.get(QStringLiteral("Cleanup"), QStringLiteral("DesktopShortcut")) != QLatin1String("1"))
        return;
    const QString path = QDir(commonDesktopDir()).filePath(appName_ + QStringLiteral(".lnk"));
    if (!QFile::exists(path)) {
        log_(tr_("桌面快捷方式不存在，跳过。"));
        return;
    }
    if (QFile::remove(path))
        log_(tr_("已删除桌面快捷方式：%1").arg(path));
    else {
        failures_++;
        manualCleanup_.append(path);
        log_(tr_("[错误] 删除桌面快捷方式失败：%1").arg(path));
    }
}

void UninstallRunner::removeRegistryKey()
{
    IniFile ini(iniPath_);
    const QString keyName = ini.get(QStringLiteral("Cleanup"), QStringLiteral("RegistryKeyName"));
    if (keyName.isEmpty()) {
        log_(tr_("未配置注册表项，跳过。"));
        return;
    }
    const QString view = ini.get(QStringLiteral("Cleanup"), QStringLiteral("RegistryView"), QStringLiteral("64"));
    const QString uninstallKey =
        QStringLiteral("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + keyName;

    if (!regKeyExists(uninstallKey, view)) {
        log_(tr_("注册表卸载项不存在，跳过。"));
        return;
    }
    QString err;
    if (regDeleteTree(uninstallKey, true, view, &err))
        log_(tr_("已删除注册表卸载项：%1").arg(keyName));
    else {
        failures_++;
        log_(tr_("[错误] 删除注册表卸载项失败：%1").arg(err));
    }
}

void UninstallRunner::removeExtraPaths()
{
    IniFile ini(iniPath_);
    for (int i = 0; ; ++i) {
        const QString p = ini.get(QStringLiteral("Cleanup"),
                                  QStringLiteral("ExtraPath%1").arg(i));
        if (p.isEmpty())
            break;
        if (QDir(p).exists()) {
            if (QDir(p).removeRecursively())
                log_(tr_("已删除目录：%1").arg(p));
            else {
                failures_++;
                manualCleanup_.append(p);
                log_(tr_("[错误] 删除失败 %1").arg(p));
            }
        } else if (QFile::exists(p)) {
            if (QFile::remove(p))
                log_(tr_("已删除文件：%1").arg(p));
            else {
                failures_++;
                manualCleanup_.append(p);
                log_(tr_("[错误] 删除失败 %1").arg(p));
            }
        } else {
            log_(tr_("路径不存在，跳过：%1").arg(p));
        }
    }
}

void UninstallRunner::deleteInstallDirContents()
{
    IniFile ini(iniPath_);
    if (ini.get(QStringLiteral("Cleanup"), QStringLiteral("DeleteInstallDir")) != QLatin1String("1")) {
        log_(tr_("按配置跳过安装目录删除。"));
        return;
    }
    if (!QDir(installDir_).exists()) {
        log_(tr_("安装目录不存在，跳过。"));
        return;
    }

    const QString baseDir = QCoreApplication::applicationDirPath();
    const QStringList ownFiles = {
        selfExePath_,
        QDir(baseDir).filePath(QStringLiteral("uninstall.exe")),
        QDir(baseDir).filePath(QStringLiteral("ESPUninstall.exe")),
        QDir(baseDir).filePath(QStringLiteral("uninstall.ini")),
    };
    auto isOwnFile = [&](const QString &entry) {
        for (const QString &o : ownFiles)
            if (samePath(entry, o))
                return true;
        return false;
    };

    const QDir dir(installDir_);
    for (const QFileInfo &fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        const QString entry = fi.absoluteFilePath();
        if (samePath(entry, selfExePath_))
            continue;
        bool ok = fi.isDir() ? QDir(entry).removeRecursively() : QFile::remove(entry);
        if (ok) {
            log_(tr_("已删除：%1").arg(entry));
        } else if (isOwnFile(entry)) {
            log_(tr_("[信息] 卸载程序自身文件暂未删除（将由延迟清理处理）：%1").arg(entry));
        } else {
            failures_++;
            manualCleanup_.append(entry);
            log_(tr_("[警告] 无法删除（可能被占用）：%1").arg(entry));
        }
    }

    scheduleSelfDelete();
    log_(tr_("已安排延迟清理（本程序退出后自动重试删除残留）。"));
}

void UninstallRunner::scheduleSelfDelete()
{
    // 隐藏 PowerShell 循环重试删除整个安装目录（含自身 exe 与 ini），
    // 与 ProgramReg 的 ScheduleSelfDelete 一致。
    const QString escaped = QString(installDir_).replace(QLatin1Char('\''), QStringLiteral("''"));
    const QString script =
        QStringLiteral("$d = '%1'; for ($i=0; $i -lt 60 -and (Test-Path -LiteralPath $d); $i++) { "
                       "Remove-Item -LiteralPath $d -Recurse -Force -ErrorAction SilentlyContinue; "
                       "if (Test-Path -LiteralPath $d) { Start-Sleep -Seconds 2 } }").arg(escaped);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    const std::wstring cmdLine =
        QStringLiteral("powershell.exe -NoProfile -NonInteractive -Command \"%1\"")
            .arg(QString(script).replace(QLatin1Char('"'), QStringLiteral("\\\"")))
            .toStdWString();
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        failures_++;
        log_(tr_("[错误] 安排延迟清理失败。"));
    }
}

} // namespace esp
