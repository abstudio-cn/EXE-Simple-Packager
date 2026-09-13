// ESPUninstall — ESP 三件套之卸载器（与安装向导同风格）
#include "UninstallWindow.h"

#include "../Shared/Theme.h"
#include "../Shared/UninstallRunner.h"

#include <QApplication>
#include <QFont>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QTextStream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

using namespace esp;

namespace {
bool isUserAdmin()
{
    return IsUserAnAdmin() != FALSE;
}

// 以管理员权限重启自身（ShellExecute runas 触发 UAC）。返回 false 表示用户取消或失败。
bool relaunchElevated(const QStringList &args)
{
    const QString exe = QCoreApplication::applicationFilePath();
    QString cmdLine;
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a.contains(QLatin1Char(' ')) || a.contains(QLatin1Char('"')))
            cmdLine += QStringLiteral(" \"%1\"").arg(a);
        else
            cmdLine += QLatin1Char(' ') + a;
    }
    const std::wstring exeW = exe.toStdWString();
    const std::wstring cmdW = cmdLine.toStdWString();
    const HINSTANCE h = ShellExecuteW(nullptr, L"runas", exeW.c_str(), cmdW.c_str(),
                                      nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(h) > 32;
}

// 静默卸载：/quiet [/ini=path]；日志写 %TEMP%\ESPUninstall.log
int quietUninstall(const QStringList &args)
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
    QString iniPath = QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("uninstall.ini"));
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args[i].compare(QLatin1String("/ini"), Qt::CaseInsensitive) == 0
            || args[i].compare(QLatin1String("--ini"), Qt::CaseInsensitive) == 0)
            iniPath = args[i + 1];
    }
    const QString logPath = QDir::temp().filePath(QStringLiteral("ESPUninstall.log"));
    QFile log(logPath);
    log.open(QIODevice::Append | QIODevice::Text);

    UninstallRunner runner(iniPath, [&log](const QString &line) {
        QTextStream ts(&log);
        ts << line << '\n';
        ts.flush();
        printf("%s\n", qPrintable(line));
        fflush(stdout);
    });
    runner.run();
    return runner.failures() == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyleSheet(globalStyleSheet());
    app.setWindowIcon(appIcon(32));
    installTranslations(app);

    QFont f(QStringLiteral("Segoe UI"), 10);
    app.setFont(f);

    const QStringList args = app.arguments().mid(1);

    // 卸载需要管理员权限（删 Program Files 文件 / HKLM 注册表）：
    // 非提权启动时以 runas 重启自身，用户取消则给出友好提示并退出，
    // 而不是让控制面板显示「没有足够的权限卸载…」。
    if (!isUserAdmin()) {
        if (relaunchElevated(app.arguments()))
            return 0;  // 已发起提权实例
        bool quiet = false;
        for (const QString &a : args)
            if (a.compare(QLatin1String("/quiet"), Qt::CaseInsensitive) == 0
                || a.compare(QLatin1String("--quiet"), Qt::CaseInsensitive) == 0)
                quiet = true;
        if (!quiet) {
            QMessageBox::warning(nullptr,
                QCoreApplication::translate("UninstallWindow", "卸载程序"),
                QCoreApplication::translate("UninstallWindow",
                    "卸载需要管理员权限，请在权限提示中选择「是」后重试。"));
        }
        return 5;
    }

    for (const QString &a : args) {
        if (a.compare(QLatin1String("/quiet"), Qt::CaseInsensitive) == 0
            || a.compare(QLatin1String("--quiet"), Qt::CaseInsensitive) == 0)
            return quietUninstall(args);
    }

    QString iniPath = QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("uninstall.ini"));
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args[i].compare(QLatin1String("/ini"), Qt::CaseInsensitive) == 0
            || args[i].compare(QLatin1String("--ini"), Qt::CaseInsensitive) == 0)
            iniPath = args[i + 1];
    }

    UninstallWindow w(iniPath);
    w.show();
    return app.exec();
}
