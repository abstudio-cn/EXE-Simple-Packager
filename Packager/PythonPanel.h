#pragma once
// PythonPanel — 打包器中的「Python 打包」页
//
// 反阻塞设计（源自 2026-09-21 的「未响应」事故）：
//  * 启动时**不做任何探测**；只有勾选「打包为 Python 应用」或点「重新探测」才触发；
//  * 探测（含 py -0p / python --version 等会启动进程的动作）全部在 PythonDetectTask 工作线程，
//    带 8 秒超时，Store 别名与 0 字节占位文件直接跳过；
//  * UI 线程只负责刷新下拉框与状态文字。

#include "../Shared/PythonEnv.h"
#include "../Shared/PythonPack.h"

#include <QMetaType>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

// 后台探测线程：候选发现 + 版本校验
class PythonDetectTask : public QThread
{
    Q_OBJECT
public:
    explicit PythonDetectTask(QObject *parent = nullptr);

signals:
    void done(const QVector<esp::PythonInterpreter> &list);

protected:
    void run() override;
};

class PythonPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PythonPanel(QWidget *parent = nullptr);

    // 读取界面配置（enabled=false 表示不按 Python 应用打包）
    esp::PythonPackSpec spec() const;
    void setLauncherDir(const QString &dir) { launcherDir_ = dir; }

    // 源目录变化：自动猜测入口脚本（main.py / app.py / 唯一 .py）
    void setProjectDir(const QString &dir);

    // 打包前校验；失败时 error 说明原因
    bool validate(QString *error, QStringList *warnings) const;

    // CLI 直接设置（跳过探测）
    void setEnabled(bool on);
    void setInterpreter(const QString &pythonExe);
    void setEntryScript(const QString &script);
    void setExtraArgs(const QString &args);
    void setConsole(bool on);
    void setIncludeSitePackages(bool on);
    void setIconPath(const QString &ico);

    // 重新计算运行时规模并刷新界面（CLI 设置参数后调用）
    void refreshRuntimeInfo();

signals:
    void changed();

private:
    void buildUi();
    void refreshEnabled();
    void startDetect();
    void onDetectDone(const QVector<esp::PythonInterpreter> &list);
    void updateRuntimeInfo();
    void browseInterpreter();
    void browseScript();
    void browseIcon();
    void clearIcon();

    QString launcherDir_;
    QString projectDir_;
    QVector<esp::PythonInterpreter> detected_;
    bool detecting_ = false;

    QCheckBox *enableChk_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QComboBox *interpCombo_ = nullptr;
    QPushButton *detectBtn_ = nullptr;
    QPushButton *browsePythonBtn_ = nullptr;
    QLineEdit *scriptEdit_ = nullptr;
    QPushButton *scriptBtn_ = nullptr;
    QLineEdit *argsEdit_ = nullptr;
    QCheckBox *consoleChk_ = nullptr;
    QCheckBox *siteChk_ = nullptr;
    QLineEdit *iconEdit_ = nullptr;
    QPushButton *iconBtn_ = nullptr;
    QPushButton *iconClearBtn_ = nullptr;
    QLabel *runtimeLabel_ = nullptr;
};
