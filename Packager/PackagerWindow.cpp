#include "PackagerWindow.h"

#include "../Shared/Theme.h"
#include "../Shared/PkgFormat.h"
#include "../Shared/ZipHelper.h"
#include "../Shared/IconExtract.h"

#include <QApplication>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QFileDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
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
    // 1. 收集卸载器运行时（静态构建：仅需 ESPUninstall.exe 一个文件）
    const QDir stubDir = QFileInfo(in_.stubExe).absoluteDir();
    const QStringList runtimeFiles = {
        QStringLiteral("ESPUninstall.exe"),
    };
    QVector<QPair<QString, QString>> extras;
    for (const QString &rel : runtimeFiles) {
        const QString disk = stubDir.filePath(rel);
        if (!QFileInfo::exists(disk)) {
            emit logLine(tr("[错误] 缺少卸载器：%1（请确认 ESPUninstall.exe 与 ESPSetup.exe 位于同一目录）").arg(rel));
            emit finishedFail(tr("缺少卸载器：%1").arg(rel));
            return;
        }
        extras.append({QStringLiteral("__esp_uninstall/") + rel, disk});
    }

    // 2. 压缩源目录 + 卸载器运行时
    emit logLine(tr("正在压缩 %1 ...").arg(in_.sourceDir));
    QString err;
    const QByteArray zip = zipDirectoryWithExtras(in_.sourceDir, extras,
        [this](int done, int total) {
            emit progress(done, total);
            if (total > 0 && done % 25 == 0)
                emit logLine(tr("压缩进度 %1/%2 ...").arg(done).arg(total));
        }, &err);
    if (zip.isEmpty()) {
        emit logLine(tr("[错误] ") + err);
        emit finishedFail(err);
        return;
    }
    emit logLine(tr("压缩完成：%1 个文件，ZIP 大小 %2 KB。")
                     .arg(QFileInfo(in_.sourceDir).fileName())
                     .arg(zip.size() / 1024.0, 0, 'f', 1));

    // 3. 写出封包（含打包时提取的主程序图标）
    PkgMeta meta;
    meta.appName = in_.appName;
    meta.version = in_.version;
    meta.publisher = in_.publisher;
    meta.appId = in_.appId;
    meta.mainExeName = in_.mainExeName;

    if (!in_.mainExeName.isEmpty()) {
        const QString exePath = QDir(in_.sourceDir)
            .filePath(QString(in_.mainExeName).replace(QLatin1Char('/'), QDir::separator()));
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
    win_ = new ModernWindow(tr("ESPackager · 简易程序打包器"), QSize(880, 680), nullptr, this);
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
    clay->setContentsMargins(28, 20, 28, 20);
    clay->setSpacing(12);

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
    addFieldRow(4, 0, tr("输出安装包"), outEdit_, tr("浏览…"), &PackagerWindow::browseOut);
    formLay->setColumnStretch(0, 1);
    formLay->setColumnStretch(1, 1);
    clay->addWidget(form);

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
    logView_->setMinimumHeight(120);
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
    // 内容较多：标题+副标题+表单(4行)+日志+按钮，需要足够高度避免底部控件被裁剪
    win_->setFixedClientSize(880, 700);

    connect(srcEdit_, &QLineEdit::editingFinished, this, &PackagerWindow::populateAutoFields);
    connect(nameEdit_, &QLineEdit::editingFinished, this, &PackagerWindow::populateAutoFields);
}

void PackagerWindow::populateAutoFields()
{
    const QString src = QDir::cleanPath(srcEdit_->text().trimmed());
    if (!QDir(src).exists())
        return;
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

void PackagerWindow::startPack()
{
    if (task_ && task_->isRunning())
        return;

    const QString src = QDir::cleanPath(srcEdit_->text().trimmed());
    if (src.isEmpty() || !QDir(src).exists()) {
        QMessageBox::warning(win_, tr("打包器"), tr("请选择有效的源目录。"));
        return;
    }
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(win_, tr("打包器"), tr("请填写产品名称。"));
        return;
    }
    const QString stub = QDir::cleanPath(stubEdit_->text().trimmed());
    if (stub.isEmpty() || !QFileInfo::exists(stub)) {
        QMessageBox::warning(win_, tr("打包器"), tr("请选择有效的安装器存根 ESPSetup.exe。"));
        return;
    }
    QString out = QDir::cleanPath(outEdit_->text().trimmed());
    if (out.isEmpty()) {
        const QString dir = QFileInfo(src).absolutePath();
        out = QDir(dir).filePath(name + QStringLiteral(".exe"));
        outEdit_->setText(QDir::toNativeSeparators(out));
    }
    if (QFileInfo::exists(out) && !QFile::remove(out)) {
        QMessageBox::warning(win_, tr("打包器"),
                             tr("输出文件已存在且无法覆盖：%1").arg(out));
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

    logView_->clear();
    progress_->setVisible(true);
    progress_->setValue(0);
    setBusy(true);

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
        if (!autoMode_) {
            QMessageBox::information(win_, tr("打包器"),
                tr("打包成功！\n\n%1\n大小：%2 KB").arg(outPath).arg(size / 1024.0, 0, 'f', 1));
        } else {
            printf("OK %s %lld\n", qPrintable(outPath), qint64(size));
            fflush(stdout);
            QApplication::exit(0);
        }
        task_->deleteLater();
        task_ = nullptr;
    });
    connect(task_, &PackTask::finishedFail, this, [this](const QString &err) {
        setBusy(false);
        logView_->appendPlainText(tr("✘ 打包失败：%1").arg(err));
        if (!autoMode_) {
            QMessageBox::critical(win_, tr("打包器"), tr("打包失败：\n%1").arg(err));
        } else {
            printf("FAIL %s\n", qPrintable(err));
            fflush(stdout);
            QApplication::exit(1);
        }
        task_->deleteLater();
        task_ = nullptr;
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
    if (busy)
        packBtn_->setText(tr("打包中…"));
    else
        packBtn_->setText(tr("开始打包"));
}

bool PackagerWindow::parseArgs(const QStringList &args)
{
    QString dir, out, stub, name, ver, pub, mainExe, appId;
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
    }
    if (dir.isEmpty() || out.isEmpty())
        return false;

    srcEdit_->setText(QDir::toNativeSeparators(dir));
    nameEdit_->setText(name.isEmpty() ? QFileInfo(dir).fileName() : name);
    verEdit_->setText(ver.isEmpty() ? QStringLiteral("1.0.0") : ver);
    pubEdit_->setText(pub);
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
    autoMode_ = true;
    QTimer::singleShot(0, this, &PackagerWindow::startPack);
    return true;
}
