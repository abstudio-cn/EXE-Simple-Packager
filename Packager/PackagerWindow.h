#pragma once
#include "../Shared/PythonPack.h"
#include "../Shared/ThemeSpec.h"

#include <QWidget>
#include <QString>
#include <QThread>

class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTabWidget;
class ThemeEditor;
class PythonPanel;

namespace esp { class ModernWindow; }

// 打包任务（后台线程）
class PackTask : public QThread
{
    Q_OBJECT
public:
    struct Input
    {
        QString sourceDir;
        QString stubExe;
        QString outExe;
        QString appName;
        QString version;
        QString publisher;
        QString mainExeName;
        QString appId;
        esp::ThemeStyle installerTheme;    // 安装器外观（配色 + 背景图）
        esp::ThemeStyle uninstallerTheme;  // 卸载器外观（配色 + 背景图）
        esp::PythonPackSpec python;        // Python 应用打包（enabled=false 时忽略）
    };
    explicit PackTask(const Input &in, QObject *parent = nullptr);

signals:
    void progress(int done, int total);
    void logLine(const QString &line);
    void finishedOk(const QString &outPath, qint64 outSize);
    void finishedFail(const QString &error);

protected:
    void run() override;

private:
    Input in_;
};

class PackagerWindow : public QWidget
{
    Q_OBJECT
public:
    explicit PackagerWindow(QWidget *parent = nullptr);

    // 显示内部 ModernWindow（包装类的 show() 转发）
    void show();

    // 命令行参数解析。autoStart=true 时解析后立即开始打包（/auto 模式）。
    bool parseArgs(const QStringList &args, bool autoStart = true);

    // 按当前主题导出效果图：<out> 去掉扩展名后生成 _installer.png / _uninstaller.png
    int exportThemePreview(const QString &outPng);

private slots:
    void browseSource();
    void browseStub();
    void browseOut();
    void browseMainExe();
    void startPack();
    void onLog(const QString &line);

private:
    QString detectMainExe(const QString &dir, QString *hint);
    void setBusy(bool busy);
    void populateAutoFields();
    void onPythonChanged();
    // 命令行 Python 参数 → 界面（含切到「Python 打包」页并刷新运行时规模）
    void applyPythonArgs(const QString &pyPack, const QString &pyInterp, const QString &pyScript,
                         const QString &pyArgs, const QString &pyConsole, const QString &pySite,
                         const QString &pyIcon, const QString &dir);
    // 参数校验失败：/auto（脚本）模式直接输出 FAIL 并退出，绝不弹模态框卡住脚本
    void reportValidationError(const QString &msg);
    void applyThemeArgs(ThemeEditor *editor, const QString &preset, const QString &colors,
                        const QString &bg, const QString &fit, const QString &opacity);

    esp::ModernWindow *win_ = nullptr;
    QLineEdit *srcEdit_ = nullptr;
    QLineEdit *nameEdit_ = nullptr;
    QLineEdit *verEdit_ = nullptr;
    QLineEdit *pubEdit_ = nullptr;
    QLineEdit *mainExeEdit_ = nullptr;
    QLineEdit *appIdEdit_ = nullptr;
    QLineEdit *stubEdit_ = nullptr;
    QLineEdit *outEdit_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QPushButton *packBtn_ = nullptr;
    QTabWidget *themeTabs_ = nullptr;
    ThemeEditor *installThemeEditor_ = nullptr;
    ThemeEditor *uninstallThemeEditor_ = nullptr;
    PythonPanel *pythonPanel_ = nullptr;

    PackTask *task_ = nullptr;
    bool autoMode_ = false;
};
