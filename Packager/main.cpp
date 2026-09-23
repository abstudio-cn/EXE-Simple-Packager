// ESPackager — ESP 三件套之打包器
#include "PackagerWindow.h"
#include "../Shared/PythonEnv.h"
#include "../Shared/PythonPack.h"
#include "../Shared/Theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
void attachConsole()
{
    // 已被调用方重定向（管道 / 文件）时直接沿用现有句柄；
    // 否则 AttachConsole + freopen("CONOUT$") 会把输出丢回控制台，脚本就抓不到。
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE)
        return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}

// /list-python：列出探测到的解释器（含被跳过的假 python），供脚本/排查用
// 每个可用候选都会真实跑一次 --version 校验（带超时），卡死的解释器会被标记为不可用。
int listPythons()
{
    attachConsole();
    QVector<esp::PythonInterpreter> list = esp::findPythonCandidates();
    esp::appendLauncherCandidates(&list);   // 命令行模式：可以启动 py.exe
    int usable = 0;
    for (esp::PythonInterpreter &it : list) {
        if (it.usable) {
            // 真实校验：卡住的解释器（如 Store 别名、Sleep 不返回的占位程序）会在这里超时
            QString ver;
            QString err;
            if (esp::probePythonVersion(it.exePath, &ver, &err, 8000)) {
                if (!ver.isEmpty())
                    it.version = ver;
            } else {
                it.usable = false;
                it.note = err;
            }
        }
        if (it.usable) {
            usable++;
            quint64 bytes = 0;
            QStringList warn;
            const QStringList rels = esp::pythonRuntimeRelPaths(it.home, false, &bytes, &warn);
            printf("OK   %-9s %s  (运行时 %d 文件 / %.1f MB)\n",
                   qPrintable(it.version.isEmpty() ? QStringLiteral("?") : it.version),
                   qPrintable(it.exePath), int(rels.size()), bytes / 1024.0 / 1024.0);
        } else {
            printf("SKIP %-9s %s  (%s)\n", "-", qPrintable(it.exePath), qPrintable(it.note));
        }
    }
    printf("TOTAL usable=%d, all=%d\n", usable, int(list.size()));
    fflush(stdout);
    return usable > 0 ? 0 : 1;
}
} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // 打包器自身使用默认外观（安装器/卸载器的外观由打包器里的主题编辑器决定）
    app.setStyleSheet(esp::globalStyleSheet(esp::ThemeStyle()));
    app.setWindowIcon(esp::appIcon(32));
    esp::installTranslations(app);

    QFont f(QStringLiteral("Segoe UI"), 10);
    app.setFont(f);

    const QStringList args = app.arguments().mid(1);
    for (const QString &a : args) {
        if (a.compare(QLatin1String("/list-python"), Qt::CaseInsensitive) == 0)
            return listPythons();
    }

    PackagerWindow w;

    QString exportPath;
    bool autoMode = false;
    for (const QString &a : args) {
        if (a.startsWith(QLatin1String("/export-preview="), Qt::CaseInsensitive)) {
            exportPath = a.mid(QStringLiteral("/export-preview=").size());
        } else if (a.compare(QLatin1String("/auto"), Qt::CaseInsensitive) == 0) {
            autoMode = true;
        }
    }

    // 命令行导出主题效果图（验收 / 文档用），不进入事件循环
    if (!exportPath.isEmpty()) {
        attachConsole();
        w.parseArgs(args, false);
        return w.exportThemePreview(exportPath);
    }

    if (autoMode) {
        // 命令行模式：连回父控制台以输出结果，解析参数并自动开始打包
        attachConsole();
        w.parseArgs(args, true);
    } else {
        // 图形模式也解析参数：让命令行指定的源目录 / 主题 / Python 打包配置直接反映到界面
        attachConsole();
        w.parseArgs(args, false);
        w.show();
    }
    return app.exec();
}
