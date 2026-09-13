#include "UninstallWindow.h"

#include "../Shared/Theme.h"
#include "../Shared/IniFile.h"
#include "../Shared/UninstallRunner.h"
#include "../Shared/IconExtract.h"

#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QCloseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QStyle>

using namespace esp;

namespace {
QPixmap checkPixmap(int size, const QColor &color)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(0, 0, size, size);
    QPen pen(Qt::white, size * 0.12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(size * 0.28, size * 0.52), QPointF(size * 0.44, size * 0.68));
    p.drawLine(QPointF(size * 0.44, size * 0.68), QPointF(size * 0.74, size * 0.34));
    return pm;
}

QPixmap crossPixmap(int size, const QColor &color)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(0, 0, size, size);
    QPen pen(Qt::white, size * 0.11, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(size * 0.30, size * 0.30), QPointF(size * 0.70, size * 0.70));
    p.drawLine(QPointF(size * 0.70, size * 0.30), QPointF(size * 0.30, size * 0.70));
    return pm;
}

QWidget *makeSidebar(const QString &title, const QString &subtitle, const QStringList &steps,
                   const QPixmap &logo, QVector<QLabel *> *stepLabels)
{
    auto *sb = new QWidget;
    sb->setProperty("role", "sidebar");
    sb->setFixedWidth(228);
    auto *lay = new QVBoxLayout(sb);
    lay->setContentsMargins(20, 26, 20, 24);
    lay->setSpacing(0);

    auto *icon = new QLabel(sb);
    icon->setPixmap(logo.isNull() ? appPixmap(44)
                                  : logo.scaled(44, 44, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lay->addWidget(icon);
    lay->addSpacing(14);

    auto *t = new QLabel(title, sb);
    t->setProperty("role", "sidebarTitle");
    lay->addWidget(t);

    auto *st = new QLabel(subtitle, sb);
    st->setProperty("role", "sidebarSub");
    lay->addWidget(st);
    lay->addSpacing(22);

    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QWidget(sb);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(10);

        auto *num = new QLabel(QString::number(i + 1), row);
        num->setFixedSize(20, 20);
        num->setAlignment(Qt::AlignCenter);
        num->setStyleSheet(QStringLiteral(
            "background: rgba(255,255,255,0.18); color: rgba(255,255,255,0.85);"
            "border-radius: 10px; font-size: 11px; font-weight: 600;"));
        rl->addWidget(num);

        auto *txt = new QLabel(steps[i], row);
        txt->setProperty("role", "sidebarStep");
        rl->addWidget(txt, 1);
        lay->addWidget(row);
        lay->addSpacing(12);
        stepLabels->append(txt);
    }
    lay->addStretch();
    return sb;
}

void highlightStep(QVector<QLabel *> &labels, int current)
{
    for (int i = 0; i < labels.size(); ++i)
        labels[i]->setProperty("role", i == current ? "sidebarStepActive" : "sidebarStep");
    for (QLabel *l : labels)
        l->style()->unpolish(l), l->style()->polish(l);
}
} // namespace

// =====================================================================
// UninstallTask
// =====================================================================

UninstallTask::UninstallTask(const QString &iniPath, QObject *parent)
    : QThread(parent), iniPath_(iniPath)
{
}

void UninstallTask::run()
{
    UninstallRunner runner(iniPath_,
        [this](const QString &line) { emit logLine(line); },
        [this](int p) { emit progress(p); });
    runner.run();
    emit done(runner.failures(), runner.manualCleanupItems());
}

// =====================================================================
// UninstallWindow
// =====================================================================

UninstallWindow::UninstallWindow(const QString &iniPath, QWidget *parent)
    : QWidget(parent), iniPath_(iniPath)
{
    IniFile ini(iniPath);
    appName_ = ini.get(QStringLiteral("AppInfo"), QStringLiteral("AppName"),
                       tr("此应用程序"));
    installDir_ = ini.get(QStringLiteral("Cleanup"), QStringLiteral("InstallDir"),
                          QCoreApplication::applicationDirPath());

    win_ = new ModernWindow(tr("卸载 · %1").arg(appName_), QSize(880, 560), nullptr, this);
    connect(win_, &ModernWindow::closeClicked, this, [this] {
        if (working_) {
            QMessageBox::information(win_, tr("卸载程序"),
                tr("卸载进行中，请等待完成后再退出。"));
            return;
        }
        win_->close();
    });

    // 从已安装的主程序中提取图标（侧栏/确认页/标题栏），失败回退内置图标
    {
        const QString mainExeName = ini.get(QStringLiteral("AppInfo"), QStringLiteral("MainExe"));
        if (!mainExeName.isEmpty()) {
            const QString mainExePath = QDir(installDir_).filePath(mainExeName);
            if (QFile::exists(mainExePath))
                mainIcon_ = extractExeIcon(mainExePath, 64);
        }
    }
    if (mainIcon_.isNull())
        mainIcon_ = appPixmap(64);
    win_->setTitleBarIcon(mainIcon_);
    win_->setWindowIcon(QIcon(mainIcon_));

    const QStringList steps = {tr("确认卸载"), tr("正在卸载"), tr("完成")};
    QVector<QLabel *> stepLabels;
    QWidget *sidebar = makeSidebar(tr("卸载程序"), tr("卸载向导"), steps, mainIcon_, &stepLabels);
    stepLabels_ = stepLabels;
    win_->setSidebar(sidebar);

    // ---- 内容 ----
    auto *content = new QWidget(win_);
    auto *clay = new QVBoxLayout(content);
    clay->setContentsMargins(0, 0, 0, 0);
    clay->setSpacing(0);

    stack_ = new QStackedWidget(content);

    // ===== 页 0：确认 =====
    auto *page0 = new QWidget;
    auto *p0 = new QVBoxLayout(page0);
    p0->setContentsMargins(36, 30, 36, 24);
    p0->setSpacing(10);
    auto *logo = new QLabel(page0);
    logo->setPixmap(mainIcon_.isNull() ? appPixmap(72)
                                       : mainIcon_.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);
    p0->addWidget(logo);
    p0->addSpacing(6);
    auto *t0 = new QLabel(tr("确定要卸载 %1 吗？").arg(appName_), page0);
    t0->setProperty("role", "pageTitle");
    t0->setAlignment(Qt::AlignCenter);
    p0->addWidget(t0);
    auto *d0 = new QLabel(tr("将从此计算机移除该程序及其相关文件：\n%1").arg(installDir_), page0);
    d0->setProperty("role", "pageSub");
    d0->setAlignment(Qt::AlignCenter);
    d0->setWordWrap(true);
    p0->addWidget(d0);
    p0->addStretch();
    stack_->addWidget(page0);

    // ===== 页 1：进度 =====
    auto *page1 = new QWidget;
    auto *p1 = new QVBoxLayout(page1);
    p1->setContentsMargins(36, 30, 36, 24);
    p1->setSpacing(12);
    auto *t1 = new QLabel(tr("正在卸载 %1 ...").arg(appName_), page1);
    t1->setProperty("role", "pageTitle");
    p1->addWidget(t1);
    progress_ = new QProgressBar(page1);
    progress_->setRange(0, 100);
    progress_->setFixedHeight(10);
    progress_->setTextVisible(false);
    p1->addWidget(progress_);
    logView_ = new QPlainTextEdit(page1);
    logView_->setProperty("role", "log");
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    p1->addWidget(logView_, 1);
    stack_->addWidget(page1);

    // ===== 页 2：完成 =====
    auto *page2 = new QWidget;
    auto *p2 = new QVBoxLayout(page2);
    p2->setContentsMargins(36, 30, 36, 24);
    p2->setSpacing(10);
    finishIcon_ = new QLabel(page2);
    finishIcon_->setAlignment(Qt::AlignCenter);
    p2->addWidget(finishIcon_);
    p2->addSpacing(6);
    finishTitle_ = new QLabel(page2);
    finishTitle_->setProperty("role", "pageTitle");
    finishTitle_->setAlignment(Qt::AlignCenter);
    p2->addWidget(finishTitle_);
    finishSub_ = new QLabel(page2);
    finishSub_->setProperty("role", "pageSub");
    finishSub_->setAlignment(Qt::AlignCenter);
    finishSub_->setWordWrap(true);
    p2->addWidget(finishSub_);
    p2->addStretch();
    stack_->addWidget(page2);

    clay->addWidget(stack_, 1);

    // ---- 底部按钮栏 ----
    auto *btnBar = new QWidget(content);
    // 用 ID 选择器限定范围，避免 QWidget 规则级联覆盖按钮自身的应用级样式
    btnBar->setObjectName(QStringLiteral("btnBar"));
    btnBar->setStyleSheet(QStringLiteral("QWidget#btnBar { background: #FAFBFE; border-top: 1px solid #ECF0F7; }"));
    auto *blay = new QHBoxLayout(btnBar);
    blay->setContentsMargins(24, 14, 24, 14);
    blay->setSpacing(10);
    blay->addStretch();

    cancelBtn_ = new QPushButton(tr("取消"), btnBar);
    cancelBtn_->setProperty("role", "secondary");
    cancelBtn_->setFixedHeight(38);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] { win_->close(); });
    blay->addWidget(cancelBtn_);

    uninstallBtn_ = new QPushButton(tr("卸载"), btnBar);
    uninstallBtn_->setProperty("role", "danger");
    uninstallBtn_->setFixedHeight(38);
    connect(uninstallBtn_, &QPushButton::clicked, this, &UninstallWindow::startUninstall);
    blay->addWidget(uninstallBtn_);

    finishBtn_ = new QPushButton(tr("完成"), btnBar);
    finishBtn_->setProperty("role", "primary");
    finishBtn_->setFixedHeight(38);
    connect(finishBtn_, &QPushButton::clicked, this, [this] { win_->close(); });
    finishBtn_->setVisible(false);
    blay->addWidget(finishBtn_);

    clay->addWidget(btnBar);

    win_->setContent(content);
    win_->setFixedClientSize(880, 520);

    highlightStep(stepLabels, 0);
    setPage(0);
}

void UninstallWindow::show()
{
    win_->show();
}

void UninstallWindow::setPage(int index)
{
    stack_->setCurrentIndex(index);
    highlightStep(stepLabels_, index);
    const bool working = working_;
    cancelBtn_->setVisible(!working);
    uninstallBtn_->setVisible(index == 0);
    uninstallBtn_->setEnabled(!working);
    finishBtn_->setVisible(index == 2);
}

void UninstallWindow::startUninstall()
{
    if (task_ && task_->isRunning())
        return;
    working_ = true;
    failures_ = 0;
    manualCleanup_.clear();
    win_->setClosable(false);
    logView_->clear();
    progress_->setValue(0);
    setPage(1);

    task_ = new UninstallTask(iniPath_, this);
    connect(task_, &UninstallTask::progress, this, [this](int p) { progress_->setValue(p); });
    connect(task_, &UninstallTask::logLine, this, [this](const QString &l) { logView_->appendPlainText(l); });
    connect(task_, &UninstallTask::done, this, &UninstallWindow::onDone);
    connect(task_, &UninstallTask::failed, this, &UninstallWindow::onFailed);
    task_->start();
}

void UninstallWindow::onDone(int failures, const QStringList &manualCleanup)
{
    working_ = false;
    win_->setClosable(true);
    failures_ = failures;
    manualCleanup_ = manualCleanup;
    if (task_) {
        task_->deleteLater();
        task_ = nullptr;
    }
    if (failures == 0) {
        finishIcon_->setPixmap(checkPixmap(72, QColor(0x18, 0xA0, 0x58)));
        finishTitle_->setText(tr("卸载完成！"));
        finishSub_->setText(tr("%1 已从此计算机移除。").arg(appName_));
    } else {
        finishIcon_->setPixmap(crossPixmap(72, QColor(0xE5, 0x48, 0x4D)));
        finishTitle_->setText(tr("卸载完成，但有残留"));
        QStringList lines;
        lines << tr("%1 个步骤未能自动完成：").arg(failures);
        for (const QString &item : manualCleanup)
            lines << QStringLiteral("· %1").arg(item);
        finishSub_->setText(lines.join(QStringLiteral("\n")));
    }
    setPage(2);
}

void UninstallWindow::onFailed(const QString &error)
{
    working_ = false;
    win_->setClosable(true);
    if (task_) {
        task_->deleteLater();
        task_ = nullptr;
    }
    finishIcon_->setPixmap(crossPixmap(72, QColor(0xE5, 0x48, 0x4D)));
    finishTitle_->setText(tr("卸载失败"));
    finishSub_->setText(error);
    setPage(2);
}
