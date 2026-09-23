#include "PackagerWindow.h"
#include "ThemeEditor.h"
#include "PythonPanel.h"

#include "../Shared/Theme.h"
#include "../Shared/ThemePreview.h"
#include "../Shared/PkgFormat.h"
#include "../Shared/ZipHelper.h"
#include "../Shared/IconExtract.h"
#include "../Shared/PythonPack.h"

#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>

using namespace esp;

// =====================================================================
// PackTask
// =====================================================================

PackTask::PackTask(const Input &in, QObject *parent)
    : QThread(parent), in_(in)
{
}

void PackTask::run()
{
    // 0. Python 模式：先在临时目录准备启动器（<应用名>.exe + launch.ini），
    //    再把解释器运行时与用户项目一起封包。
    QTemporaryDir stage;
    QVector<QPair<QString, QString>> extras;
    QString mainExeDisk;      // 提取图标用的主程序（普通模式=源目录内主程序；Python 模式=生成的启动器）
    QString mainExeName = in_.mainExeName;
    QString prefix;           // 归档内前缀（Python 模式项目文件放 app/）

    if (in_.python.enabled && !stage.isValid()) {
        emit logLine(tr("[错误] 无法创建临时目录。"));
        emit finishedFail(tr("无法创建临时目录"));
        return;
    }

    if (in_.python.enabled) {
        PythonPackSpec spec = in_.python;
        spec.projectDir = in_.sourceDir;
        spec.appName = in_.appName.isEmpty() ? QFileInfo(in_.sourceDir).fileName() : in_.appName;
        spec.launcherDir = in_.python.launcherDir;
        mainExeName = spec.appName + QStringLiteral(".exe");

        const QString home = QFileInfo(spec.interpreterExe).absolutePath();
        emit logLine(tr("Python 模式：解释器 %1").arg(spec.interpreterExe));

        QString launcherPath;
        QString err;
        QStringList warns;
        if (!preparePythonLauncher(spec, stage.path(), &launcherPath, &err, &warns)) {
            emit logLine(tr("[错误] ") + err);
            emit finishedFail(err);
            return;
        }
        for (const QString &w : warns)
            emit logLine(tr("[警告] ") + w);
        emit logLine(tr("已生成启动器 %1（%2 子系统）。")
                         .arg(QFileInfo(launcherPath).fileName(),
                              spec.console ? QStringLiteral("console") : QStringLiteral("GUI")));
        mainExeDisk = launcherPath;
        extras.append({mainExeName, launcherPath});
        extras.append({QStringLiteral("launch.ini"),
                       QDir(stage.path()).filePath(QStringLiteral("launch.ini"))});

        // 随包携带的解释器运行时
        quint64 bytes = 0;
        QStringList warn2;
        const QStringList rels = pythonRuntimeRelPaths(home, spec.includeSitePackages, &bytes, &warn2);
        if (rels.isEmpty()) {
            const QString e2 = tr("未能收集 Python 运行时文件：%1").arg(home);
            emit logLine(tr("[错误] ") + e2);
            emit finishedFail(e2);
            return;
        }
        for (const QString &rel : rels)
            extras.append({rel, QDir(home).filePath(rel)});
        emit logLine(tr("随包携带 Python 运行时：%1 个文件，%2 MB。")
                         .arg(rels.size())
                         .arg(bytes / 1024.0 / 1024.0, 0, 'f', 1));
        for (const QString &w : warn2)
            emit logLine(tr("[提示] ") + w);

        prefix = QStringLiteral("app/");
        emit logLine(tr("项目文件将安装到 app\\ 子目录，入口脚本 %1")
                         .arg(entryScriptInstallRelPath(spec.projectDir, spec.entryScript)));
    }

    // 1. 收集卸载器运行时（静态构建：仅需 ESPUninstall.exe 一个文件）
    const QDir stubDir = QFileInfo(in_.stubExe).absoluteDir();
    {
        const QString rel = QStringLiteral("ESPUninstall.exe");
        const QString disk = stubDir.filePath(rel);
        if (!QFileInfo::exists(disk)) {
            emit logLine(tr("[错误] 缺少卸载器：%1（请确认 ESPUninstall.exe 与 ESPSetup.exe 位于同一目录）").arg(rel));
            emit finishedFail(tr("缺少卸载器：%1").arg(rel));
            return;
        }
        extras.append({QStringLiteral("__esp_uninstall/") + rel, disk});
    }

    // 2. 压缩源目录（Python 模式置于 app/ 前缀下）+ 运行时
    emit logLine(tr("正在压缩 %1 ...").arg(in_.sourceDir));
    QString err;
    const QByteArray zip = zipDirectoryWithExtras(in_.sourceDir, extras,
        [this](int done, int total) {
            emit progress(done, total);
            if (total > 0 && done % 25 == 0)
                emit logLine(tr("压缩进度 %1/%2 ...").arg(done).arg(total));
        }, &err, prefix);
    if (zip.isEmpty()) {
        emit logLine(tr("[错误] ") + err);
        emit finishedFail(err);
        return;
    }
    emit logLine(tr("压缩完成：%1 个文件，ZIP 大小 %2 KB。")
                     .arg(QFileInfo(in_.sourceDir).fileName())
                     .arg(zip.size() / 1024.0, 0, 'f', 1));

    // 3. 写出封包（含打包时提取的主程序图标 + 自定义外观主题）
    PkgMeta meta;
    meta.appName = in_.appName;
    meta.version = in_.version;
    meta.publisher = in_.publisher;
    meta.appId = in_.appId;
    meta.mainExeName = mainExeName;
    meta.theme.installer = in_.installerTheme;
    meta.theme.uninstaller = in_.uninstallerTheme;

    if (meta.theme.installer.hasBackground())
        emit logLine(tr("安装器背景图 %1 KB（%2，不透明度 %3%）。")
                         .arg(meta.theme.installer.bgImage.size() / 1024.0, 0, 'f', 1)
                         .arg(bgFitToString(meta.theme.installer.bgFit))
                         .arg(meta.theme.installer.bgOpacity));
    if (meta.theme.uninstaller.hasBackground())
        emit logLine(tr("卸载器背景图 %1 KB（%2，不透明度 %3%）。")
                         .arg(meta.theme.uninstaller.bgImage.size() / 1024.0, 0, 'f', 1)
                         .arg(bgFitToString(meta.theme.uninstaller.bgFit))
                         .arg(meta.theme.uninstaller.bgOpacity));
    emit logLine(tr("安装器主色 %1 / 副色 %2；卸载器主色 %3 / 副色 %4。")
                     .arg(meta.theme.installer.primary.name())
                     .arg(meta.theme.installer.secondary.name())
                     .arg(meta.theme.uninstaller.primary.name())
                     .arg(meta.theme.uninstaller.secondary.name()));

    if (!mainExeName.isEmpty()) {
        const QString exePath = mainExeDisk.isEmpty()
                                    ? QDir(in_.sourceDir).filePath(QString(mainExeName).replace(QLatin1Char('/'), QDir::separator()))
                                    : mainExeDisk;
        meta.iconPng = extractExeIconPng(exePath, 64);
        if (meta.iconPng.isEmpty())
            emit logLine(tr("[警告] 未能提取主程序图标：%1，安装器将使用默认图标。").arg(exePath));
        else
            emit logLine(tr("已提取主程序图标（%1 字节），随封包内嵌。").arg(meta.iconPng.size()));
    }

    emit logLine(tr("正在生成安装包 %1 ...").arg(in_.outExe));
    if (!writePackage(in_.stubExe, zip, meta, in_.outExe, &err)) {
        emit logLine(tr("[错误] ") + err);
        emit finishedFail(err);
        return;
    }
    const qint64 size = QFileInfo(in_.outExe).size();
    emit logLine(tr("安装包生成完成，大小 %1 KB。").arg(size / 1024.0, 0, 'f', 1));
    emit finishedOk(in_.outExe, size);
}

// =====================================================================
// PackagerWindow
// =====================================================================

static QWidget *makeField(const QString &labelText, QWidget *edit, QWidget *browse,
                          QWidget **outRow)
{
    auto *row = new QWidget;
    auto *lay = new QVBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(5);
    auto *label = new QLabel(labelText, row);
    label->setProperty("role", "fieldLabel");
    lay->addWidget(label);
    auto *hline = new QWidget(row);
    auto *hlay = new QHBoxLayout(hline);
    hlay->setContentsMargins(0, 0, 0, 0);
    hlay->setSpacing(8);
    hlay->addWidget(edit, 1);
    if (browse)
        hlay->addWidget(browse);
    lay->addWidget(hline);
    if (outRow)
        *outRow = row;
    return row;
}

PackagerWindow::PackagerWindow(QWidget *parent)
    : QWidget(parent)
{
    win_ = new ModernWindow(tr("ESPackager · 简易程序打包器"), QSize(980, 800), nullptr, this);
    connect(win_, &ModernWindow::closeClicked, this, [this] {
        if (task_ && task_->isRunning()) {
            QMessageBox::information(win_, tr("打包器"),
                tr("打包进行中，请等待完成后再退出。"));
            return;
        }
        win_->close();
    });

    auto *content = new QWidget(win_);
    auto *clay = new QVBoxLayout(content);
    clay->setContentsMargins(28, 18, 28, 18);
    clay->setSpacing(10);

    auto *title = new QLabel(tr("创建安装包"), content);
    title->setProperty("role", "pageTitle");
    clay->addWidget(title);

    auto *sub = new QLabel(tr("将程序目录与卸载器运行时压缩封包，生成单一可执行安装程序（ESPPKG01 格式）。"),
                           content);
    sub->setProperty("role", "pageSub");
    sub->setWordWrap(true);
    clay->addWidget(sub);
    clay->addSpacing(4);

    // ---- 表单 ----
    auto *form = new QWidget(content);
    auto *formLay = new QGridLayout(form);
    formLay->setContentsMargins(0, 0, 0, 0);
    formLay->setHorizontalSpacing(18);
    formLay->setVerticalSpacing(10);

    auto addFieldRow = [&](int row, int col, const QString &label, QWidget *edit,
                           const QString &btnText, auto slot) {
        auto *browse = new QPushButton(btnText, form);
        browse->setProperty("role", "secondary");
        browse->setFixedHeight(36);
        connect(browse, &QPushButton::clicked, this, slot);
        QWidget *ignored = nullptr;
        formLay->addWidget(makeField(label, edit, browse, &ignored), row, col);
    };

    srcEdit_ = new QLineEdit(form);
    srcEdit_->setPlaceholderText(tr("选择要打包的程序目录"));
    addFieldRow(0, 0, tr("源目录"), srcEdit_, tr("浏览…"), &PackagerWindow::browseSource);

    nameEdit_ = new QLineEdit(form);
    nameEdit_->setPlaceholderText(tr("例如：My Application"));
    formLay->addWidget(makeField(tr("产品名称"), nameEdit_, nullptr, nullptr), 0, 1);

    verEdit_ = new QLineEdit(form);
    verEdit_->setText(QStringLiteral("1.0.0"));
    formLay->addWidget(makeField(tr("版本"), verEdit_, nullptr, nullptr), 1, 0);

    pubEdit_ = new QLineEdit(form);
    pubEdit_->setPlaceholderText(tr("例如：My Company"));
    formLay->addWidget(makeField(tr("发行商"), pubEdit_, nullptr, nullptr), 1, 1);

    mainExeEdit_ = new QLineEdit(form);
    mainExeEdit_->setPlaceholderText(tr("自动检测源目录中的 exe"));
    addFieldRow(2, 0, tr("主程序 exe"), mainExeEdit_, tr("选择…"), &PackagerWindow::browseMainExe);

    appIdEdit_ = new QLineEdit(form);
    appIdEdit_->setPlaceholderText(tr("默认使用产品名称"));
    formLay->addWidget(makeField(tr("注册表卸载键名"), appIdEdit_, nullptr, nullptr), 2, 1);

    stubEdit_ = new QLineEdit(form);
    stubEdit_->setText(QDir(QApplication::applicationDirPath()).filePath(QStringLiteral("ESPSetup.exe")));
    addFieldRow(3, 0, tr("安装器存根 (ESPSetup.exe)"), stubEdit_, tr("浏览…"), &PackagerWindow::browseStub);

    outEdit_ = new QLineEdit(form);
    outEdit_->setPlaceholderText(tr("默认输出到源目录上级：产品名.exe"));
    addFieldRow(3, 1, tr("输出安装包"), outEdit_, tr("浏览…"), &PackagerWindow::browseOut);
    formLay->setColumnStretch(0, 1);
    formLay->setColumnStretch(1, 1);
    clay->addWidget(form);

    // ---- 外观主题：安装器 / 卸载器各自可自定义配色与背景图 ----
    themeTabs_ = new QTabWidget(content);
    themeTabs_->setToolTip(tr("自定义安装器 / 卸载器的配色与背景图，右侧为实时预览"));
    installThemeEditor_ = new ThemeEditor(false, themeTabs_);
    uninstallThemeEditor_ = new ThemeEditor(true, themeTabs_);
    themeTabs_->addTab(installThemeEditor_, tr("安装器主题"));
    themeTabs_->addTab(uninstallThemeEditor_, tr("卸载器主题"));
    pythonPanel_ = new PythonPanel(themeTabs_);
    pythonPanel_->setLauncherDir(QApplication::applicationDirPath());
    themeTabs_->addTab(pythonPanel_, tr("Python 打包"));
    connect(pythonPanel_, &PythonPanel::changed, this, &PackagerWindow::onPythonChanged);
    clay->addWidget(themeTabs_);

    // ---- 进度 + 日志 ----
    progress_ = new QProgressBar(content);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setVisible(false);
    clay->addWidget(progress_);

    logView_ = new QPlainTextEdit(content);
    logView_->setProperty("role", "log");
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(2000);
    logView_->setMinimumHeight(50);
    clay->addWidget(logView_, 1);

    // ---- 按钮 ----
    auto *btnRow = new QWidget(content);
    auto *blay = new QHBoxLayout(btnRow);
    blay->setContentsMargins(0, 0, 0, 0);
    blay->addStretch();

    packBtn_ = new QPushButton(tr("开始打包"), btnRow);
    packBtn_->setProperty("role", "primary");
    packBtn_->setFixedHeight(40);
    connect(packBtn_, &QPushButton::clicked, this, &PackagerWindow::startPack);
    blay->addWidget(packBtn_);
    clay->addWidget(btnRow);

    win_->setContent(content);
    // 内容较多：标题+副标题+表单(5行)+主题编辑器+日志+按钮。
    // 高度按可用屏幕收窄（显示分辨率可能只有 900 高），日志区用最小高度吸收剩余空间。
    win_->setFixedClientSize(980, 800);

    connect(srcEdit_, &QLineEdit::editingFinished, this, &PackagerWindow::populateAutoFields);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &PackagerWindow::populateAutoFields);
    // 产品名称变化时同步更新两个主题预览与 Python 入口名
    connect(nameEdit_, &QLineEdit::textChanged, this, [this](const QString &t) {
        const QString name = t.trimmed();
        installThemeEditor_->setPreviewAppName(name);
        uninstallThemeEditor_->setPreviewAppName(name);
        onPythonChanged();
    });
}

void PackagerWindow::populateAutoFields()
{
    const QString src = QDir::cleanPath(srcEdit_->text().trimmed());
    if (!QDir(src).exists())
        return;
    pythonPanel_->setProjectDir(src);
    QString hint;
    const QString mainExe = detectMainExe(src, &hint);
    if (!mainExe.isEmpty() && mainExeEdit_->text().trimmed().isEmpty()) {
        mainExeEdit_->setText(mainExe);
        if (!hint.isEmpty())
            logView_->appendPlainText(hint);
    }
    if (outEdit_->text().trimmed().isEmpty() && !nameEdit_->text().trimmed().isEmpty()) {
        const QString dir = QFileInfo(src).absolutePath();
        outEdit_->setText(QDir(dir).filePath(nameEdit_->text().trimmed() + QStringLiteral(".exe")));
    }
    if (appIdEdit_->text().trimmed().isEmpty() && !nameEdit_->text().trimmed().isEmpty())
        appIdEdit_->setText(nameEdit_->text().trimmed());
}

QString PackagerWindow::detectMainExe(const QString &dir, QString *hint)
{
    const QDir d(dir);
    const auto isEspName = [](const QString &base) {
        return base == QLatin1String("ESPSetup") || base == QLatin1String("ESPUninstall")
            || base == QLatin1String("ESPackager") || base == QLatin1String("uninstall");
    };

    // 1. 根目录优先
    QStringList exes;
    for (const QFileInfo &fi : d.entryInfoList(QDir::Files)) {
        if (fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0
            && !isEspName(fi.completeBaseName()))
            exes.append(fi.fileName());
    }
    // 2. 根目录没有则递归查找（深度限 3 层）
    if (exes.isEmpty()) {
        QStringList relExes;
        const auto walk = [&](auto &&self, const QDir &cur, const QString &rel, int depth) -> void {
            if (depth > 3)
                return;
            for (const QFileInfo &fi : cur.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (fi.isDir()) {
                    self(self, QDir(fi.absoluteFilePath()),
                         rel.isEmpty() ? fi.fileName() : rel + QLatin1Char('/') + fi.fileName(), depth + 1);
                } else if (fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) == 0
                           && !isEspName(fi.completeBaseName())) {
                    relExes.append(rel.isEmpty() ? fi.fileName() : rel + QLatin1Char('/') + fi.fileName());
                }
            }
        };
        walk(walk, d, QString(), 1);
        exes = relExes;
    }
    if (exes.size() > 1 && hint)
        *hint = tr("[提示] 源目录含多个 exe（%1），已默认选择 %2，可手动更改。")
                    .arg(exes.join(QStringLiteral(", ")), exes.first());
    return exes.isEmpty() ? QString() : exes.first();
}

void PackagerWindow::show()
{
    win_->show();
}

void PackagerWindow::browseSource()
{
    const QString dir = QFileDialog::getExistingDirectory(win_, tr("选择要打包的程序目录"),
                                                          srcEdit_->text());
    if (!dir.isEmpty()) {
        srcEdit_->setText(QDir::toNativeSeparators(dir));
        populateAutoFields();
    }
}

// Python 打包页变化：自动填写/锁定主程序名（Python 应用的入口就是生成的 <应用名>.exe）
void PackagerWindow::onPythonChanged()
{
    const PythonPackSpec spec = pythonPanel_->spec();
    if (spec.enabled) {
        const QString name = nameEdit_->text().trimmed().isEmpty()
                                 ? QStringLiteral("MyApp") : nameEdit_->text().trimmed();
        mainExeEdit_->setText(name + QStringLiteral(".exe"));
        mainExeEdit_->setEnabled(false);
        mainExeEdit_->setToolTip(tr("Python 应用：入口为打包生成的 <应用名>.exe（随包启动器）"));
    } else {
        mainExeEdit_->setEnabled(true);
        mainExeEdit_->setToolTip(QString());
    }
}

void PackagerWindow::browseStub()
{
    const QString f = QFileDialog::getOpenFileName(win_, tr("选择安装器存根"),
                                                   stubEdit_->text(), QStringLiteral("可执行文件 (*.exe)"));
    if (!f.isEmpty())
        stubEdit_->setText(QDir::toNativeSeparators(f));
}

void PackagerWindow::browseOut()
{
    QString f = QFileDialog::getSaveFileName(win_, tr("输出安装包"), outEdit_->text(),
                                             QStringLiteral("安装程序 (*.exe)"));
    if (!f.isEmpty()) {
        if (!f.toLower().endsWith(QLatin1String(".exe")))
            f += QStringLiteral(".exe");
        outEdit_->setText(QDir::toNativeSeparators(f));
    }
}

void PackagerWindow::browseMainExe()
{
    const QString f = QFileDialog::getOpenFileName(win_, tr("选择主程序"),
                                                   srcEdit_->text(), QStringLiteral("可执行文件 (*.exe)"));
    if (!f.isEmpty()) {
        const QDir src(srcEdit_->text());
        if (src.exists() && QFileInfo(f).absolutePath().startsWith(src.absolutePath()))
            mainExeEdit_->setText(src.relativeFilePath(f));
        else
            mainExeEdit_->setText(QFileInfo(f).fileName());
    }
}

void PackagerWindow::reportValidationError(const QString &msg)
{
    // /auto（脚本）模式绝不能弹模态框：否则脚本会一直等一个没人点的确认框
    if (autoMode_) {
        printf("FAIL %s\n", qPrintable(msg));
        fflush(stdout);
        QApplication::exit(1);
        return;
    }
    QMessageBox::warning(win_, tr("打包器"), msg);
}

void PackagerWindow::startPack()
{
    if (task_ && task_->isRunning())
        return;

    const QString src = QDir::cleanPath(srcEdit_->text().trimmed());
    if (src.isEmpty() || !QDir(src).exists()) {
        reportValidationError(tr("请选择有效的源目录。"));
        return;
    }
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        reportValidationError(tr("请填写产品名称。"));
        return;
    }
    const QString stub = QDir::cleanPath(stubEdit_->text().trimmed());
    if (stub.isEmpty() || !QFileInfo::exists(stub)) {
        reportValidationError(tr("请选择有效的安装器存根 ESPSetup.exe。"));
        return;
    }
    QString out = QDir::cleanPath(outEdit_->text().trimmed());
    if (out.isEmpty()) {
        const QString dir = QFileInfo(src).absolutePath();
        out = QDir(dir).filePath(name + QStringLiteral(".exe"));
        outEdit_->setText(QDir::toNativeSeparators(out));
    }
    if (QFileInfo::exists(out) && !QFile::remove(out)) {
        reportValidationError(tr("输出文件已存在且无法覆盖：%1").arg(out));
        return;
    }

    // Python 应用打包校验（解释器 / 入口脚本 / 图标）
    QString pyErr;
    QStringList pyWarn;
    if (!pythonPanel_->validate(&pyErr, &pyWarn)) {
        reportValidationError(pyErr);
        return;
    }

    PackTask::Input in;
    in.sourceDir = src;
    in.stubExe = stub;
    in.outExe = out;
    in.appName = name;
    in.version = verEdit_->text().trimmed().isEmpty() ? QStringLiteral("1.0.0") : verEdit_->text().trimmed();
    in.publisher = pubEdit_->text().trimmed();
    in.mainExeName = mainExeEdit_->text().trimmed();
    in.appId = appIdEdit_->text().trimmed().isEmpty() ? name : appIdEdit_->text().trimmed();
    in.installerTheme = installThemeEditor_->style();
    in.uninstallerTheme = uninstallThemeEditor_->style();
    in.python = pythonPanel_->spec();
    if (in.python.enabled) {
        in.python.projectDir = src;
        in.python.appName = name;
        in.python.launcherDir = QApplication::applicationDirPath();
        in.mainExeName = name + QStringLiteral(".exe");
    }

    logView_->clear();
    progress_->setVisible(true);
    progress_->setValue(0);
    setBusy(true);
    for (const QString &w : pyWarn)
        logView_->appendPlainText(tr("[提示] ") + w);

    task_ = new PackTask(in, this);
    connect(task_, &PackTask::progress, this, [this](int done, int total) {
        progress_->setRange(0, total > 0 ? total : 1);
        progress_->setValue(done);
    });
    connect(task_, &PackTask::logLine, this, &PackagerWindow::onLog);
    connect(task_, &PackTask::finishedOk, this, [this](const QString &outPath, qint64 size) {
        setBusy(false);
        progress_->setValue(progress_->maximum());
        logView_->appendPlainText(tr("✔ 打包成功：%1").arg(outPath));
        // 关键：等工作线程真正退出后再继续。否则（大负载时尤其明显）/auto 模式直接
        // 退出进程会在 QThread 尚在运行时析构父对象，触发 qFatal → 进程以 0xC0000409 结束。
        task_->wait();
        task_->deleteLater();
        task_ = nullptr;
        if (!autoMode_) {
            QMessageBox::information(win_, tr("打包器"),
                tr("打包成功！\n\n%1\n大小：%2 KB").arg(outPath).arg(size / 1024.0, 0, 'f', 1));
        } else {
            printf("OK %s %lld\n", qPrintable(outPath), qint64(size));
            fflush(stdout);
            QApplication::exit(0);
        }
    });
    connect(task_, &PackTask::finishedFail, this, [this](const QString &err) {
        setBusy(false);
        logView_->appendPlainText(tr("✘ 打包失败：%1").arg(err));
        task_->wait();
        task_->deleteLater();
        task_ = nullptr;
        if (!autoMode_) {
            QMessageBox::critical(win_, tr("打包器"), tr("打包失败：\n%1").arg(err));
        } else {
            printf("FAIL %s\n", qPrintable(err));
            fflush(stdout);
            QApplication::exit(1);
        }
    });
    task_->start();
}

void PackagerWindow::onLog(const QString &line)
{
    logView_->appendPlainText(line);
}

void PackagerWindow::setBusy(bool busy)
{
    packBtn_->setEnabled(!busy);
    win_->setClosable(!busy);
    srcEdit_->setEnabled(!busy);
    nameEdit_->setEnabled(!busy);
    verEdit_->setEnabled(!busy);
    pubEdit_->setEnabled(!busy);
    mainExeEdit_->setEnabled(!busy);
    appIdEdit_->setEnabled(!busy);
    stubEdit_->setEnabled(!busy);
    outEdit_->setEnabled(!busy);
    if (themeTabs_)
        themeTabs_->setEnabled(!busy);
    if (busy)
        packBtn_->setText(tr("打包中…"));
    else
        packBtn_->setText(tr("开始打包"));
}

void PackagerWindow::applyPythonArgs(const QString &pyPack, const QString &pyInterp,
                                     const QString &pyScript, const QString &pyArgs,
                                     const QString &pyConsole, const QString &pySite,
                                     const QString &pyIcon, const QString &dir)
{
    if (!pythonPanel_)
        return;
    const bool any = !pyPack.isEmpty() || !pyInterp.isEmpty() || !pyScript.isEmpty()
                     || !pyIcon.isEmpty();
    if (!any)
        return;
    if (!dir.isEmpty())
        pythonPanel_->setProjectDir(QDir::cleanPath(dir));
    if (!pyPack.isEmpty())
        pythonPanel_->setEnabled(pyPack != QLatin1String("0"));
    if (!pyInterp.isEmpty())
        pythonPanel_->setInterpreter(QDir::cleanPath(pyInterp));
    if (!pyScript.isEmpty())
        pythonPanel_->setEntryScript(pyScript);
    if (!pyArgs.isEmpty())
        pythonPanel_->setExtraArgs(pyArgs);
    if (!pyConsole.isEmpty())
        pythonPanel_->setConsole(pyConsole != QLatin1String("0"));
    if (!pySite.isEmpty())
        pythonPanel_->setIncludeSitePackages(pySite != QLatin1String("0"));
    if (!pyIcon.isEmpty())
        pythonPanel_->setIconPath(QDir::cleanPath(pyIcon));
    // 命令行指定了 Python 参数：界面切到该页并刷新运行时规模（自动模式下窗口不显示）
    if (themeTabs_)
        themeTabs_->setCurrentWidget(pythonPanel_);
    pythonPanel_->refreshRuntimeInfo();
}

void PackagerWindow::applyThemeArgs(ThemeEditor *editor, const QString &preset, const QString &colors,
                                    const QString &bg, const QString &fit, const QString &opacity)
{
    if (!editor)
        return;
    ThemeStyle s = editor->style();
    if (!preset.isEmpty() && !applyPreset(preset, &s))
        logView_->appendPlainText(tr("[警告] 未知的主题预设：%1").arg(preset));
    if (!colors.isEmpty()) {
        const QStringList parts = colors.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            const QColor p(parts[0].trimmed()), sec(parts[1].trimmed());
            if (p.isValid() && sec.isValid()) {
                s.primary = p;
                s.secondary = sec;
            } else {
                logView_->appendPlainText(tr("[警告] 颜色格式非法（应为 #RRGGBB）：%1").arg(colors));
            }
        }
        if (parts.size() >= 4) {
            const QColor s1(parts[2].trimmed()), s2(parts[3].trimmed());
            if (s1.isValid())
                s.sidebarStart = s1;
            if (s2.isValid())
                s.sidebarEnd = s2;
        }
    }
    if (!bg.isEmpty()) {
        QByteArray encoded;
        QSize px;
        QString err;
        if (encodeBackgroundFile(bg, 2560, &encoded, &px, &err)) {
            s.bgImage = encoded;
            s.bgImageName = QFileInfo(bg).fileName();
            logView_->appendPlainText(tr("已载入背景图 %1（%2×%3，%4 KB）。")
                                          .arg(s.bgImageName)
                                          .arg(px.width())
                                          .arg(px.height())
                                          .arg(encoded.size() / 1024.0, 0, 'f', 1));
        } else {
            logView_->appendPlainText(tr("[错误] ") + err);
        }
    }
    if (!fit.isEmpty())
        s.bgFit = bgFitFromString(fit);
    if (!opacity.isEmpty()) {
        bool ok = false;
        const int v = opacity.toInt(&ok);
        if (ok)
            s.bgOpacity = qBound(0, v, 100);
    }
    editor->setStyle(s);
}

bool PackagerWindow::parseArgs(const QStringList &args, bool autoStart)
{
    QString dir, out, stub, name, ver, pub, mainExe, appId;
    QString iPreset, iColors, iBg, iFit, iOpacity;
    QString uPreset, uColors, uBg, uFit, uOpacity;
    QString pyPack, pyInterp, pyScript, pyArgs, pyConsole, pySite, pyIcon;
    for (const QString &a : args) {
        const int eq = a.indexOf('=');
        if (eq <= 0)
            continue;
        const QString key = a.left(eq).toLower();
        const QString val = a.mid(eq + 1);
        if (key == QLatin1String("/dir")) dir = val;
        else if (key == QLatin1String("/out")) out = val;
        else if (key == QLatin1String("/stub")) stub = val;
        else if (key == QLatin1String("/name")) name = val;
        else if (key == QLatin1String("/ver")) ver = val;
        else if (key == QLatin1String("/pub")) pub = val;
        else if (key == QLatin1String("/mainexe")) mainExe = val;
        else if (key == QLatin1String("/appid")) appId = val;
        else if (key == QLatin1String("/instpreset")) iPreset = val;
        else if (key == QLatin1String("/instcolor")) iColors = val;
        else if (key == QLatin1String("/instbg")) iBg = val;
        else if (key == QLatin1String("/instfit")) iFit = val;
        else if (key == QLatin1String("/instopacity")) iOpacity = val;
        else if (key == QLatin1String("/uninstpreset")) uPreset = val;
        else if (key == QLatin1String("/uninstcolor")) uColors = val;
        else if (key == QLatin1String("/uninstbg")) uBg = val;
        else if (key == QLatin1String("/uninstfit")) uFit = val;
        else if (key == QLatin1String("/uninstopacity")) uOpacity = val;
        else if (key == QLatin1String("/pypack")) pyPack = val;
        else if (key == QLatin1String("/python")) pyInterp = val;
        else if (key == QLatin1String("/pyscript")) pyScript = val;
        else if (key == QLatin1String("/pyargs")) pyArgs = val;
        else if (key == QLatin1String("/pyconsole")) pyConsole = val;
        else if (key == QLatin1String("/pysite")) pySite = val;
        else if (key == QLatin1String("/pyicon")) pyIcon = val;
    }

    // 外观主题参数（界面预览与打包共用同一份状态）
    if (!iPreset.isEmpty() || !iColors.isEmpty() || !iBg.isEmpty()
        || !iFit.isEmpty() || !iOpacity.isEmpty())
        applyThemeArgs(installThemeEditor_, iPreset, iColors, iBg, iFit, iOpacity);
    if (!uPreset.isEmpty() || !uColors.isEmpty() || !uBg.isEmpty()
        || !uFit.isEmpty() || !uOpacity.isEmpty())
        applyThemeArgs(uninstallThemeEditor_, uPreset, uColors, uBg, uFit, uOpacity);

    // GUI 模式（autoStart=false）：即使参数不全，也把已给出的部分反映到界面
    if (dir.isEmpty() || out.isEmpty()) {
        if (!autoStart) {
            if (!dir.isEmpty())
                srcEdit_->setText(QDir::toNativeSeparators(dir));
            if (!name.isEmpty())
                nameEdit_->setText(name);
            if (!ver.isEmpty())
                verEdit_->setText(ver);
            if (!pub.isEmpty())
                pubEdit_->setText(pub);
            if (!mainExe.isEmpty())
                mainExeEdit_->setText(mainExe);
            if (!appId.isEmpty())
                appIdEdit_->setText(appId);
            if (!stub.isEmpty())
                stubEdit_->setText(QDir::toNativeSeparators(stub));
            if (!out.isEmpty())
                outEdit_->setText(QDir::toNativeSeparators(out));
            applyPythonArgs(pyPack, pyInterp, pyScript, pyArgs, pyConsole, pySite, pyIcon, dir);
        } else {
            logView_->appendPlainText(tr("[错误] /auto 模式需要 /dir 与 /out 参数。"));
        }
        return false;
    }

    srcEdit_->setText(QDir::toNativeSeparators(dir));
    nameEdit_->setText(name.isEmpty() ? QFileInfo(dir).fileName() : name);
    verEdit_->setText(ver.isEmpty() ? QStringLiteral("1.0.0") : ver);
    pubEdit_->setText(pub);
    applyPythonArgs(pyPack, pyInterp, pyScript, pyArgs, pyConsole, pySite, pyIcon, dir);
    if (mainExe.isEmpty()) {
        // 未指定主程序 → 自动检测源目录中的 exe（含子目录）
        QString hint;
        mainExe = detectMainExe(dir, &hint);
    } else {
        // 归一化路径分隔符（与 zip 条目一致）
        mainExe.replace(QLatin1Char('\\'), QLatin1Char('/'));
    }
    mainExeEdit_->setText(mainExe);
    appIdEdit_->setText(appId);
    if (!stub.isEmpty())
        stubEdit_->setText(QDir::toNativeSeparators(stub));
    outEdit_->setText(QDir::toNativeSeparators(out));
    autoMode_ = autoStart;
    if (autoStart)
        QTimer::singleShot(0, this, &PackagerWindow::startPack);
    return true;
}

int PackagerWindow::exportThemePreview(const QString &outPng)
{
    // 导出安装器 / 卸载器两张效果图（与运行时窗口同一套绘制实现，WYSIWYG）
    QString base = outPng;
    if (base.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        base.chop(4);
    const QString instPath = base + QStringLiteral("_installer.png");
    const QString uninstPath = base + QStringLiteral("_uninstaller.png");
    QDir().mkpath(QFileInfo(instPath).absolutePath());

    const QPixmap inst = renderMock(QSize(960, 560), installThemeEditor_->style(),
                                    installThemeEditor_->previewSpec());
    const QPixmap uninst = renderMock(QSize(880, 520), uninstallThemeEditor_->style(),
                                      uninstallThemeEditor_->previewSpec());
    const bool ok = inst.save(instPath, "PNG") && uninst.save(uninstPath, "PNG");
    if (ok)
        printf("OK %s\nOK %s\n", qPrintable(instPath), qPrintable(uninstPath));
    else
        printf("FAIL 无法写入预览图：%s\n", qPrintable(base));
    fflush(stdout);
    return ok ? 0 : 1;
}
