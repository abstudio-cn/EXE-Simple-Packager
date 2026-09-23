#pragma once
// PythonPack — 把 Python 项目打包为自包含应用（随包携带解释器）
//
// 产物布局（安装目录）：
//   <AppName>.exe       ESPLauncher 复本（可选替换为应用自带图标）—— 入口，进程名即应用名
//   launch.ini          Dll / Script / Args / Home
//   python3XX.dll  DLLs/  Lib/  tcl/        随包携带的解释器运行时
//   app/                用户项目文件（入口脚本默认 app\main.py）
//
// 运行时文件清单是纯文件系统扫描；复制/压缩由调用方在工作线程完成。

#include <QString>
#include <QStringList>

namespace esp {

struct PythonPackSpec
{
    bool enabled = false;
    QString appName;              // 生成 <appName>.exe
    QString projectDir;           // 用户项目目录（源目录）
    QString interpreterExe;       // python.exe
    QString entryScript;          // 入口脚本（相对 projectDir 或绝对路径）
    QString args;                 // 入口脚本附加参数
    bool console = false;         // true：控制台子系统启动器（可见控制台）
    bool includeSitePackages = false;
    QString iconPath;             // 可选 .ico（替换启动器图标）
    QString launcherDir;          // ESPLauncher.exe / ESPLauncherC.exe 所在目录（打包器同目录）
};

// 运行时文件清单（返回相对 home 的归档路径，'/' 分隔）；totalBytes 输出总字节数
QStringList pythonRuntimeRelPaths(const QString &home, bool includeSitePackages,
                                  quint64 *totalBytes, QStringList *warnings);

// 选择启动器模板（控制台版 / 窗口版）
QString launcherTemplatePath(const PythonPackSpec &spec, QString *error);

// 在 destDir 生成 <appName>.exe（复制模板 + 可选替换图标）与 launch.ini。
// warnings 收集非致命问题（如图标替换失败已回退默认图标）。
bool preparePythonLauncher(const PythonPackSpec &spec, const QString &destDir,
                           QString *launcherPath, QString *error,
                           QStringList *warnings = nullptr);

// 用 .ico 替换 exe 的图标资源（Win32 UpdateResource；失败不影响主流程）
bool replaceExeIcon(const QString &exePath, const QString &icoPath, QString *error);

// 入口脚本相对安装目录的路径（供 launch.ini 使用，'\' 分隔）
QString entryScriptInstallRelPath(const QString &projectDir, const QString &entryScript);

} // namespace esp
