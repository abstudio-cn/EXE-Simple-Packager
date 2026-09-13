// ESPackager — ESP 三件套之打包器
#include "PackagerWindow.h"
#include "../Shared/Theme.h"

#include <QApplication>
#include <QFont>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyleSheet(esp::globalStyleSheet());
    app.setWindowIcon(esp::appIcon(32));
    esp::installTranslations(app);

    QFont f(QStringLiteral("Segoe UI"), 10);
    app.setFont(f);

    PackagerWindow w;

    const QStringList args = app.arguments().mid(1);
    bool autoMode = false;
    for (const QString &a : args) {
        if (a.compare(QLatin1String("/auto"), Qt::CaseInsensitive) == 0) {
            // 命令行模式：连回父控制台以输出结果，解析参数并自动开始打包
            if (AttachConsole(ATTACH_PARENT_PROCESS)) {
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);
            }
            w.parseArgs(args);
            autoMode = true;
            break;
        }
    }
    if (!autoMode)
        w.show();
    return app.exec();
}
