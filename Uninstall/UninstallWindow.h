#pragma once
#include <QWidget>
#include <QString>
#include <QStringList>
#include <QThread>

class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QLabel;

namespace esp { class ModernWindow; }

// 卸载任务（后台线程）
class UninstallTask : public QThread
{
    Q_OBJECT
public:
    explicit UninstallTask(const QString &iniPath, QObject *parent = nullptr);

signals:
    void progress(int percent);
    void logLine(const QString &line);
    void done(int failures, const QStringList &manualCleanup);
    void failed(const QString &error);

protected:
    void run() override;

private:
    QString iniPath_;
};

// 与安装向导同风格的卸载器
class UninstallWindow : public QWidget
{
    Q_OBJECT
public:
    explicit UninstallWindow(const QString &iniPath, QWidget *parent = nullptr);

    // 显示内部 ModernWindow
    void show();

private slots:
    void startUninstall();
    void onDone(int failures, const QStringList &manualCleanup);
    void onFailed(const QString &error);

private:
    void setPage(int index);

    esp::ModernWindow *win_ = nullptr;
    QString iniPath_;
    QString appName_;
    QString installDir_;
    QPixmap mainIcon_;
    QStackedWidget *stack_ = nullptr;
    QVector<QLabel *> stepLabels_;
    QProgressBar *progress_ = nullptr;
    QPlainTextEdit *logView_ = nullptr;
    QLabel *finishIcon_ = nullptr;
    QLabel *finishTitle_ = nullptr;
    QLabel *finishSub_ = nullptr;
    QPushButton *uninstallBtn_ = nullptr;
    QPushButton *cancelBtn_ = nullptr;
    QPushButton *finishBtn_ = nullptr;
    UninstallTask *task_ = nullptr;
    bool working_ = false;
    int failures_ = 0;
    QStringList manualCleanup_;
};
