#include "PythonPanel.h"

#include "../Shared/PythonEnv.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

using namespace esp;

// =====================================================================
// PythonDetectTask
// =====================================================================

PythonDetectTask::PythonDetectTask(QObject *parent)
    : QThread(parent)
{
}

void PythonDetectTask::run()
{
    // 1) 纯扫描（注册表 / PATH / 常见目录）—— 不启动进程
    QVector<PythonInterpreter> list = findPythonCandidates();
    // 2) py 启动器（会启动 py.exe，因此只允许在工作线程里做）
    appendLauncherCandidates(&list);

    // 3) 逐个校验版本（同样只在这里；最多 6 个，单个 8 秒超时）
    int probed = 0;
    for (PythonInterpreter &it : list) {
        if (!it.usable)
            continue;
        if (probed >= 6)
            break;
        probed++;
        QString ver;
        QString err;
        if (probePythonVersion(it.exePath, &ver, &err, 8000)) {
            if (!ver.isEmpty())
                it.version = ver;
        } else {
            it.usable = false;
            it.note = err;
        }
    }
    emit done(list);
}

// =====================================================================
// PythonPanel
// =====================================================================

PythonPanel::PythonPanel(QWidget *parent)
    : QWidget(parent)
{
    // 跨线程传递探测结果需要已注册的元类型
    qRegisterMetaType<QVector<PythonInterpreter>>("QVector<esp::PythonInterpreter>");
    buildUi();
    refreshEnabled();
}

void PythonPanel::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(16, 14, 16, 14);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);

    const auto label = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setProperty("role", "fieldLabel");
        return l;
    };

    int row = 0;
    // 启用复选框 + 状态
    enableChk_ = new QCheckBox(tr("打包为 Python 应用（随包携带解释器，目标机无需安装 Python）"), this);
    connect(enableChk_, &QCheckBox::toggled, this, [this](bool on) {
        refreshEnabled();
        if (on && detected_.isEmpty() && !detecting_)
            startDetect();   // 首次勾选才探测，启动阶段零开销
        emit changed();
    });
    grid->addWidget(enableChk_, row, 0, 1, 3);

    statusLabel_ = new QLabel(tr("未探测"), this);
    statusLabel_->setProperty("role", "pageSub");
    statusLabel_->setWordWrap(true);
    grid->addWidget(statusLabel_, row, 3, 1, 3);
    row++;

    // 解释器
    grid->addWidget(label(tr("Python 解释器")), row, 0);
    interpCombo_ = new QComboBox(this);
    interpCombo_->setMinimumWidth(320);
    connect(interpCombo_, &QComboBox::currentIndexChanged, this, [this] {
        updateRuntimeInfo();
        emit changed();
    });
    grid->addWidget(interpCombo_, row, 1, 1, 2);
    detectBtn_ = new QPushButton(tr("重新探测"), this);
    detectBtn_->setProperty("role", "secondary");
    connect(detectBtn_, &QPushButton::clicked, this, &PythonPanel::startDetect);
    grid->addWidget(detectBtn_, row, 3);
    browsePythonBtn_ = new QPushButton(tr("浏览…"), this);
    browsePythonBtn_->setProperty("role", "secondary");
    connect(browsePythonBtn_, &QPushButton::clicked, this, &PythonPanel::browseInterpreter);
    grid->addWidget(browsePythonBtn_, row, 4);
    row++;

    // 入口脚本
    grid->addWidget(label(tr("入口脚本")), row, 0);
    scriptEdit_ = new QLineEdit(this);
    scriptEdit_->setPlaceholderText(tr("相对源目录，例如 main.py（项目文件会安装到 app\\ 子目录）"));
    connect(scriptEdit_, &QLineEdit::textChanged, this, [this] { emit changed(); });
    grid->addWidget(scriptEdit_, row, 1, 1, 2);
    scriptBtn_ = new QPushButton(tr("选择…"), this);
    scriptBtn_->setProperty("role", "secondary");
    connect(scriptBtn_, &QPushButton::clicked, this, &PythonPanel::browseScript);
    grid->addWidget(scriptBtn_, row, 3);
    row++;

    // 附加参数 + 控制台
    grid->addWidget(label(tr("附加参数")), row, 0);
    argsEdit_ = new QLineEdit(this);
    argsEdit_->setPlaceholderText(tr("可选，例如 --config config.json"));
    grid->addWidget(argsEdit_, row, 1, 1, 2);
    consoleChk_ = new QCheckBox(tr("显示控制台窗口"), this);
    connect(consoleChk_, &QCheckBox::toggled, this, [this] { emit changed(); });
    grid->addWidget(consoleChk_, row, 3, 1, 2);
    row++;

    // site-packages + 图标
    siteChk_ = new QCheckBox(tr("包含 site-packages（第三方依赖）"), this);
    connect(siteChk_, &QCheckBox::toggled, this, [this] {
        updateRuntimeInfo();
        emit changed();
    });
    grid->addWidget(siteChk_, row, 0, 1, 2);
    grid->addWidget(label(tr("应用图标")), row, 2);
    iconEdit_ = new QLineEdit(this);
    iconEdit_->setPlaceholderText(tr("可选 .ico，用作 exe / 快捷方式图标"));
    grid->addWidget(iconEdit_, row, 3);
    iconBtn_ = new QPushButton(tr("选择…"), this);
    iconBtn_->setProperty("role", "secondary");
    connect(iconBtn_, &QPushButton::clicked, this, &PythonPanel::browseIcon);
    grid->addWidget(iconBtn_, row, 4);
    iconClearBtn_ = new QPushButton(tr("清除"), this);
    iconClearBtn_->setProperty("role", "secondary");
    connect(iconClearBtn_, &QPushButton::clicked, this, &PythonPanel::clearIcon);
    grid->addWidget(iconClearBtn_, row, 5);
    row++;

    runtimeLabel_ = new QLabel(this);
    runtimeLabel_->setProperty("role", "pageSub");
    runtimeLabel_->setWordWrap(true);
    grid->addWidget(runtimeLabel_, row, 0, 1, 6);
}

void PythonPanel::refreshEnabled()
{
    const bool on = enableChk_->isChecked();
    const QWidget *controls[] = {interpCombo_, detectBtn_, browsePythonBtn_,
                                 scriptEdit_, scriptBtn_, argsEdit_, consoleChk_, siteChk_,
                                 iconEdit_, iconBtn_, iconClearBtn_};
    for (const QWidget *w : controls) {
        if (w)
            const_cast<QWidget *>(w)->setEnabled(on);
    }
    if (!on)
        runtimeLabel_->clear();
    else if (runtimeLabel_->text().isEmpty())
        updateRuntimeInfo();
}

void PythonPanel::startDetect()
{
    if (detecting_)
        return;
    detecting_ = true;
    statusLabel_->setText(tr("正在后台探测已安装的 Python…"));
    interpCombo_->setEnabled(false);
    detectBtn_->setEnabled(false);
    auto *task = new PythonDetectTask(this);
    connect(task, &PythonDetectTask::done, this, &PythonPanel::onDetectDone);
    connect(task, &QThread::finished, task, [task] {
        task->wait();
        task->deleteLater();
    });
    task->start();
}

void PythonPanel::onDetectDone(const QVector<PythonInterpreter> &list)
{
    detecting_ = false;
    detected_ = list;
    interpCombo_->setEnabled(enableChk_->isChecked());
    detectBtn_->setEnabled(enableChk_->isChecked());

    const QSignalBlocker block(interpCombo_);
    const QString previous = interpCombo_->currentData().toString();
    interpCombo_->clear();
    int usableCount = 0;
    int unusableCount = 0;
    for (const PythonInterpreter &it : list) {
        QString text;
        if (it.usable) {
            usableCount++;
            text = QStringLiteral("%1  ·  %2")
                       .arg(it.version.isEmpty() ? tr("版本未知") : it.version, it.exePath);
        } else {
            unusableCount++;
            text = QStringLiteral("✗ %1  ·  %2").arg(it.note, it.exePath);
        }
        interpCombo_->addItem(text, it.exePath);
        const int idx = interpCombo_->count() - 1;
        if (!it.usable)
            interpCombo_->setItemData(idx, QColor(0x9A, 0xA3, 0xB5), Qt::ForegroundRole);
    }
    if (!previous.isEmpty()) {
        const int idx = interpCombo_->findData(previous);
        if (idx >= 0)
            interpCombo_->setCurrentIndex(idx);
    }
    if (usableCount == 0)
        statusLabel_->setText(tr("未找到可用的 Python 解释器（已跳过 Store 别名与占位文件）。"));
    else
        statusLabel_->setText(tr("找到 %1 个可用解释器%2。")
                                  .arg(usableCount)
                                  .arg(unusableCount > 0
                                           ? tr("（另有 %1 个不可用，已标注）").arg(unusableCount)
                                           : QString()));
    updateRuntimeInfo();
    emit changed();
}

void PythonPanel::updateRuntimeInfo()
{
    if (!enableChk_->isChecked()) {
        runtimeLabel_->clear();
        return;
    }
    const QString home = QFileInfo(interpCombo_->currentData().toString()).absolutePath();
    if (home.isEmpty() || !QDir(home).exists()) {
        runtimeLabel_->setText(tr("请选择有效的 Python 解释器。"));
        return;
    }
    quint64 bytes = 0;
    QStringList warnings;
    const QStringList files = pythonRuntimeRelPaths(home, siteChk_->isChecked(), &bytes, &warnings);
    QString text = tr("随包携带 %1 个运行时文件，约 %2 MB（安装后占用相同空间）。")
                       .arg(files.size())
                       .arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    if (!warnings.isEmpty())
        text += QStringLiteral("  ") + warnings.join(QStringLiteral("；"));
    runtimeLabel_->setText(text);
}

void PythonPanel::setProjectDir(const QString &dir)
{
    projectDir_ = dir;
    if (!dir.isEmpty() && scriptEdit_ && scriptEdit_->text().trimmed().isEmpty())
        scriptEdit_->setText(QStringLiteral("main.py"));
}

void PythonPanel::browseInterpreter()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("选择 python.exe"),
                                                   interpCombo_->currentData().toString(),
                                                   QStringLiteral("python.exe (python*.exe);;可执行文件 (*.exe)"));
    if (f.isEmpty())
        return;
    QString reason;
    if (isUnusablePython(f, &reason)) {
        statusLabel_->setText(tr("该文件不可用：%1").arg(reason));
        return;
    }
    const QSignalBlocker block(interpCombo_);
    int idx = interpCombo_->findData(f);
    if (idx < 0) {
        const QString home = QFileInfo(f).absolutePath();
        const QString dll = findRuntimeDllName(home);
        PythonInterpreter it;
        it.exePath = f;
        it.home = home;
        it.dllName = dll;
        it.version = versionFromPath(home);
        it.usable = !dll.isEmpty();
        it.note = it.usable ? tr("手动选择") : tr("目录内未找到 python3XX.dll");
        detected_.append(it);
        interpCombo_->addItem(QStringLiteral("%1  ·  %2")
                                  .arg(it.version.isEmpty() ? tr("版本未知") : it.version, f), f);
        idx = interpCombo_->count() - 1;
    }
    interpCombo_->setCurrentIndex(idx);
    updateRuntimeInfo();
    emit changed();
}

void PythonPanel::browseScript()
{
    const QString start = projectDir_.isEmpty() ? QString() : projectDir_;
    const QString f = QFileDialog::getOpenFileName(this, tr("选择入口脚本"), start,
                                                   QStringLiteral("Python 脚本 (*.py)"));
    if (f.isEmpty())
        return;
    const QFileInfo fi(f);
    if (!projectDir_.isEmpty() && fi.absolutePath().startsWith(QDir(projectDir_).absolutePath()))
        scriptEdit_->setText(QDir(projectDir_).relativeFilePath(f));
    else
        scriptEdit_->setText(f);
    emit changed();
}

void PythonPanel::browseIcon()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("选择应用图标"), QString(),
                                                   QStringLiteral("图标文件 (*.ico)"));
    if (!f.isEmpty())
        iconEdit_->setText(QDir::toNativeSeparators(f));
    emit changed();
}

void PythonPanel::clearIcon()
{
    iconEdit_->clear();
    emit changed();
}

void PythonPanel::refreshRuntimeInfo()
{
    updateRuntimeInfo();
}

void PythonPanel::setEnabled(bool on)
{
    enableChk_->setChecked(on);
    refreshEnabled();
}

void PythonPanel::setInterpreter(const QString &pythonExe)
{
    if (pythonExe.isEmpty())
        return;
    const QSignalBlocker block(interpCombo_);
    int idx = interpCombo_->findData(pythonExe);
    if (idx < 0) {
        interpCombo_->addItem(pythonExe, pythonExe);
        idx = interpCombo_->count() - 1;
    }
    interpCombo_->setCurrentIndex(idx);
    updateRuntimeInfo();
}

void PythonPanel::setEntryScript(const QString &script)
{
    scriptEdit_->setText(script);
}

void PythonPanel::setExtraArgs(const QString &args)
{
    argsEdit_->setText(args);
}

void PythonPanel::setConsole(bool on)
{
    consoleChk_->setChecked(on);
}

void PythonPanel::setIncludeSitePackages(bool on)
{
    siteChk_->setChecked(on);
}

void PythonPanel::setIconPath(const QString &ico)
{
    iconEdit_->setText(ico);
}

PythonPackSpec PythonPanel::spec() const
{
    PythonPackSpec s;
    s.enabled = enableChk_->isChecked();
    if (!s.enabled)
        return s;
    s.interpreterExe = interpCombo_->currentData().toString().trimmed();
    s.projectDir = projectDir_;
    s.entryScript = scriptEdit_->text().trimmed();
    s.args = argsEdit_->text().trimmed();
    s.console = consoleChk_->isChecked();
    s.includeSitePackages = siteChk_->isChecked();
    s.iconPath = iconEdit_->text().trimmed();
    s.launcherDir = launcherDir_;
    return s;
}

bool PythonPanel::validate(QString *error, QStringList *warnings) const
{
    const PythonPackSpec s = spec();
    if (!s.enabled)
        return true;

    if (s.projectDir.isEmpty() || !QDir(s.projectDir).exists()) {
        if (error) *error = tr("请先选择有效的源目录。");
        return false;
    }
    if (s.interpreterExe.isEmpty() || !QFileInfo::exists(s.interpreterExe)) {
        if (error) *error = tr("请选择可用的 Python 解释器（可点「重新探测」）。");
        return false;
    }
    QString reason;
    if (isUnusablePython(s.interpreterExe, &reason)) {
        if (error) *error = tr("所选解释器不可用：%1").arg(reason);
        return false;
    }
    const QString home = QFileInfo(s.interpreterExe).absolutePath();
    if (findRuntimeDllName(home).isEmpty()) {
        if (error) *error = tr("解释器目录内未找到 python3XX.dll：%1").arg(home);
        return false;
    }
    if (s.entryScript.isEmpty()) {
        if (error) *error = tr("请填写入口脚本（例如 main.py）。");
        return false;
    }
    const QFileInfo fi(s.entryScript);
    const QString scriptPath = fi.isAbsolute() ? fi.absoluteFilePath()
                                               : QDir(s.projectDir).filePath(s.entryScript);
    if (!QFileInfo::exists(scriptPath)) {
        if (error) *error = tr("找不到入口脚本：%1").arg(scriptPath);
        return false;
    }
    if (!s.iconPath.isEmpty() && !QFileInfo::exists(s.iconPath)) {
        if (error) *error = tr("找不到图标文件：%1").arg(s.iconPath);
        return false;
    }
    if (warnings) {
        quint64 bytes = 0;
        QStringList warn;
        pythonRuntimeRelPaths(home, s.includeSitePackages, &bytes, &warn);
        *warnings = warn;
    }
    return true;
}
