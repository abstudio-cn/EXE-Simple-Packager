#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QThread>

#include "../Shared/PkgFormat.h"

class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QCheckBox;
class QStackedWidget;
class QLabel;

namespace esp {
class ModernWindow;
}

// 安装任务（后台线程）：读自身负载 → 解压安装
class InstallTask : public QThread
{
    Q_OBJECT
public:
    InstallTask(const QString &exePath, const QString &installDir,
                bool startMenu, bool desktop, bool taskbar,
                const esp::PkgMeta &meta, QObject *parent = nullptr);

signals:
    void progress(int percent);
    void logLine(const QString &line);
    void done(int failures, const QStringList &warnings);
    void failed(const QString &error);

protected:
    void run() override;

private:
    QString exePath_;
    QString installDir_;
    bool startMenu_;
    bool desktop_;
    bool taskbar_;
    esp::PkgMeta meta_;
};

// MSI 风格安装向导（上一步/下一步/取消）
class WizardWindow : public QWidget
{
    Q_OBJECT
public:
    explicit WizardWindow(const esp::PkgMeta &meta, QWidget *parent = nullptr);

    // 显示内部 ModernWindow
    void show();

private slots:
    void goNext();
    void goBack();
    void cancel();
    void browseInstallDir();

private:
    void setPage(int index);
    void updateButtons();
    void startInstall();
    void onInstallDone(int failures, const QStringList &warnings);
    void onInstallFailed(const QString &error);

    esp::ModernWindow *win_ = nullptr;
    esp::PkgMeta meta_;
    QPixmap mainIcon_;
    QStackedWidget *stack_ = nullptr;
    QWidget *sidebar_ = nullptr;
    QVector<QLabel *> stepLabels_;

    // 页面控件
    QRadioButton *agreeRadio_ = nullptr;
    QLineEdit *dirEdit_ = nullptr;
    QLabel *spaceLabel_ = nullptr;
    QCheckBox *startMenuChk_ = nullptr;
    QCheckBox *desktopChk_ = nullptr;
    QCheckBox *taskbarChk_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QLabel *finishIcon_ = nullptr;
    QLabel *finishTitle_ = nullptr;
    QLabel *finishSub_ = nullptr;
    QCheckBox *runChk_ = nullptr;

    QPushButton *backBtn_ = nullptr;
    QPushButton *nextBtn_ = nullptr;
    QPushButton *cancelBtn_ = nullptr;

    InstallTask *task_ = nullptr;
    bool installing_ = false;
    bool installFailed_ = false;
    int installFailures_ = 0;
    QStringList installWarnings_;
    int currentPage_ = 0;
};
