// ESPSetup — ESP 三件套之安装向导（MSI 风格）
#include "WizardWindow.h"

#include "../Shared/Theme.h"
#include "../Shared/PkgFormat.h"
#include "../Shared/InstallCore.h"

#include <QApplication>
#include <QFont>
#include <QDir>
#include <QFile>
#include <QMessageBox>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace esp;

namespace {
void attachParentConsole()
{
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

// 静默安装：/quiet /dir=... /agree [/startmenu=0|1] [/desktop=0|1] [/taskbar=0|1]
int quietInstall(const QStringList &args)
{
    attachParentConsole();
    const QString logPath = QDir::temp().filePath(QStringLiteral("ESPSetup.log"));
    QFile log(logPath);
    log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
    auto emitLine = [&log](const QString &line) {
        QTextStream ts(&log);
        ts << line << '\n';
        ts.flush();
        printf("%s\n", qPrintable(line));
        fflush(stdout);
    };

    QString dir;
    bool agree = false;
    bool startMenu = true, desktop = false, taskbar = false;
    for (const QString &a : args) {
        const int eq = a.indexOf('=');
        const QString key = (eq > 0 ? a.left(eq) : a).toLower();
        const QString val = eq > 0 ? a.mid(eq + 1) : QString();
        if (key == QLatin1String("/dir")) dir = val;
        else if (key == QLatin1String("/agree")) agree = true;
        else if (key == QLatin1String("/startmenu")) startMenu = (val != QLatin1String("0"));
        else if (key == QLatin1String("/desktop")) desktop = (val == QLatin1String("1"));
        else if (key == QLatin1String("/taskbar")) taskbar = (val == QLatin1String("1"));
    }
    if (dir.isEmpty()) {
        emitLine(QStringLiteral("FAIL 静默安装缺少 /dir 参数"));
        return 1;
    }
    if (!agree) {
        emitLine(QStringLiteral("FAIL 静默安装需要 /agree 参数确认接受许可协议"));
        return 2;
    }

    PkgMeta meta;
    QString err;
    if (!readPkgMeta(QApplication::applicationFilePath(), &meta, &err)) {
        emitLine(QStringLiteral("FAIL ") + err);
        return 3;
    }
    QByteArray zip;
    if (!readPayload(QApplication::applicationFilePath(), &zip, &err)) {
        emitLine(QStringLiteral("FAIL ") + err);
        return 3;
    }

    InstallOptions o;
    o.appName = meta.appName;
    o.version = meta.version;
    o.publisher = meta.publisher;
    o.installDir = QDir::cleanPath(dir);
    o.mainExeName = meta.mainExeName;
    o.appId = meta.appId;
    o.startMenuShortcut = startMenu;
    o.desktopShortcut = desktop;
    o.taskbarShortcut = taskbar;

    QStringList warnings;
    const int failures = runInstall(zip, o, emitLine, nullptr, &warnings);
    for (const QString &w : warnings)
        emitLine(QStringLiteral("WARN ") + w);
    emitLine(failures == 0 ? QStringLiteral("OK 安装完成")
                           : QStringLiteral("DONE_FAILURES %1").arg(failures));
    return failures == 0 ? 0 : 4;
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
    for (const QString &a : args) {
        if (a.compare(QLatin1String("/quiet"), Qt::CaseInsensitive) == 0
            || a.compare(QLatin1String("/s"), Qt::CaseInsensitive) == 0)
            return quietInstall(args);
    }

    PkgMeta meta;
    QString err;
    if (!readPkgMeta(QApplication::applicationFilePath(), &meta, &err)) {
        QMessageBox::critical(nullptr, QCoreApplication::translate("WizardWindow", "安装向导"), err);
        return 1;
    }

    WizardWindow w(meta);
    w.show();
    return app.exec();
}
