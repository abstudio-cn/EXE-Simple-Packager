#pragma once
#include <QWidget>
#include <QString>
#include <QThread>

class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;

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

    // 命令行 /auto 模式：解析参数并自动开始打包
    bool parseArgs(const QStringList &args);

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

    PackTask *task_ = nullptr;
    bool autoMode_ = false;
};
