#include "ThemeEditor.h"

#include "../Shared/ThemePreview.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

using namespace esp;

namespace {

// 背景图宽度上限：既保证显示质量，又让安装包体积与解码耗时可控
constexpr int kMaxBackgroundWidth = 2560;

void styleSwatch(QPushButton *b, const QColor &c)
{
    const int luma = (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
    b->setText(c.name(QColor::HexRgb).toUpper());
    b->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid #D6DCE8; border-radius: 8px;"
        " color: %2; font-family: Consolas; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { border: 1px solid #8A97AE; }")
        .arg(c.name(QColor::HexRgb), luma < 140 ? QStringLiteral("#FFFFFF") : QStringLiteral("#1F2430")));
}

} // namespace

// =====================================================================
// BackgroundImageTask
// =====================================================================

BackgroundImageTask::BackgroundImageTask(const QString &path, int maxWidth, QObject *parent)
    : QThread(parent), path_(path), maxWidth_(maxWidth)
{
}

void BackgroundImageTask::run()
{
    QByteArray encoded;
    QSize size;
    QString err;
    if (!encodeBackgroundFile(path_, maxWidth_, &encoded, &size, &err)) {
        emit failed(err);
        return;
    }
    emit loaded(encoded, size);
}

// =====================================================================
// ThemePreviewPane
// =====================================================================

ThemePreviewPane::ThemePreviewPane(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(300, 160);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ThemePreviewPane::setTheme(const ThemeStyle &style)
{
    style_ = style;
    update();
}

void ThemePreviewPane::setSpec(const MockSpec &spec)
{
    spec_ = spec;
    update();
}

void ThemePreviewPane::paintEvent(QPaintEvent *)
{
    // 背景图只在字节变化时解码一次（有界），拖动滑块等高频重绘不会重复解码
    if (decodedFor_ != style_.bgImage) {
        decodedFor_ = style_.bgImage;
        bgImage_ = style_.hasBackground()
                       ? decodeBackground(style_.bgImage, qMax(720, width() * 2))
                       : QImage();
    }
    QPainter p(this);
    paintMock(&p, rect(), style_, spec_, bgImage_);
}

// =====================================================================
// ThemeEditor
// =====================================================================

ThemeEditor::ThemeEditor(bool uninstaller, QWidget *parent)
    : QWidget(parent), uninstaller_(uninstaller)
{
    buildUi();
    refreshUi();
}

void ThemeEditor::buildUi()
{
    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(16, 14, 16, 14);
    grid->setHorizontalSpacing(18);
    grid->setVerticalSpacing(10);

    // ---------- 左：控件 ----------
    auto *left = new QWidget(this);
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(8);

    const auto makeLabel = [&](const QString &text, QWidget *parentWidget) {
        auto *l = new QLabel(text, parentWidget);
        l->setProperty("role", "fieldLabel");
        return l;
    };

    // 预设配色
    {
        auto *row = new QWidget(left);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(makeLabel(tr("预设配色"), row));
        presetCombo_ = new QComboBox(row);
        presetCombo_->setFixedHeight(30);
        for (const ThemePreset &p : themePresets())
            presetCombo_->addItem(presetDisplayName(p), QString::fromLatin1(p.id));
        presetCombo_->addItem(tr("自定义"), QString());
        connect(presetCombo_, &QComboBox::currentIndexChanged,
                this, &ThemeEditor::applyPresetIndex);
        h->addWidget(presetCombo_, 1);
        lv->addWidget(row);
    }

    // 四个配色
    {
        auto *box = new QWidget(left);
        auto *g = new QGridLayout(box);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(8);
        g->setVerticalSpacing(6);
        const QString labels[4] = {tr("主色"), tr("副色"), tr("侧栏主色"), tr("侧栏副色")};
        for (int i = 0; i < 4; ++i) {
            auto *btn = new QPushButton(box);
            btn->setProperty("role", "swatch");
            btn->setFixedHeight(28);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setToolTip(tr("点击选择颜色"));
            connect(btn, &QPushButton::clicked, this, [this, i] { pickColor(i); });
            colorButtons_[i] = btn;
            g->addWidget(makeLabel(labels[i], box), i / 2, (i % 2) * 2);
            g->addWidget(btn, i / 2, (i % 2) * 2 + 1);
        }
        g->setColumnStretch(1, 1);
        g->setColumnStretch(3, 1);
        lv->addWidget(box);
    }

    // 背景图片
    {
        auto *row = new QWidget(left);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(makeLabel(tr("背景图片"), row));
        bgChooseBtn_ = new QPushButton(tr("选择图片…"), row);
        bgChooseBtn_->setProperty("role", "secondary");
        bgChooseBtn_->setFixedHeight(30);
        connect(bgChooseBtn_, &QPushButton::clicked, this, &ThemeEditor::chooseBackground);
        h->addWidget(bgChooseBtn_);
        bgClearBtn_ = new QPushButton(tr("清除"), row);
        bgClearBtn_->setProperty("role", "secondary");
        bgClearBtn_->setFixedHeight(30);
        connect(bgClearBtn_, &QPushButton::clicked, this, &ThemeEditor::clearBackground);
        h->addWidget(bgClearBtn_);
        bgNameLabel_ = new QLabel(tr("未选择"), row);
        bgNameLabel_->setProperty("role", "pageSub");
        // 固定宽度：长文件名 / 错误信息只在提示气泡里显示，避免撑大窗口
        bgNameLabel_->setFixedWidth(170);
        h->addWidget(bgNameLabel_);
        h->addStretch();
        lv->addWidget(row);
    }

    // 填充方式 + 不透明度
    {
        auto *row = new QWidget(left);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        h->addWidget(makeLabel(tr("填充方式"), row));
        fitCombo_ = new QComboBox(row);
        fitCombo_->setFixedHeight(30);
        fitCombo_->addItem(tr("铺满（裁剪）"), QByteArray("cover"));
        fitCombo_->addItem(tr("适应（留边）"), QByteArray("contain"));
        fitCombo_->addItem(tr("拉伸"), QByteArray("stretch"));
        fitCombo_->addItem(tr("平铺"), QByteArray("tile"));
        fitCombo_->addItem(tr("居中"), QByteArray("center"));
        fitCombo_->setMinimumWidth(120);
        connect(fitCombo_, &QComboBox::currentIndexChanged, this, [this] {
            style_.bgFit = bgFitFromString(QString::fromLatin1(fitCombo_->currentData().toByteArray()));
            emitChanged();
        });
        h->addWidget(fitCombo_);
        h->addSpacing(6);
        h->addWidget(makeLabel(tr("不透明度"), row));
        opacitySlider_ = new QSlider(Qt::Horizontal, row);
        opacitySlider_->setRange(0, 100);
        opacitySlider_->setFixedWidth(120);
        connect(opacitySlider_, &QSlider::valueChanged, this, [this](int v) {
            style_.bgOpacity = v;
            opacityValue_->setText(QStringLiteral("%1%").arg(v));
            emitChanged();
        });
        h->addWidget(opacitySlider_);
        opacityValue_ = new QLabel(QStringLiteral("100%"), row);
        opacityValue_->setProperty("role", "pageSub");
        opacityValue_->setFixedWidth(38);
        h->addWidget(opacityValue_);
        h->addStretch();
        lv->addWidget(row);
    }

    grid->addWidget(left, 0, 0);

    // ---------- 右：实时预览 ----------
    auto *previewBox = new QFrame(this);
    previewBox->setProperty("role", "fieldCard");
    auto *pv = new QVBoxLayout(previewBox);
    pv->setContentsMargins(12, 10, 12, 12);
    pv->setSpacing(6);
    auto *pvTitle = new QLabel(tr("效果预览"), previewBox);
    pvTitle->setProperty("role", "fieldLabel");
    pv->addWidget(pvTitle);
    preview_ = new ThemePreviewPane(previewBox);
    pv->addWidget(preview_, 1);
    grid->addWidget(previewBox, 0, 1);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
}

void ThemeEditor::setStyle(const ThemeStyle &style)
{
    style_ = style;
    refreshUi();
}

void ThemeEditor::setPreviewAppName(const QString &appName)
{
    appName_ = appName;
    preview_->setSpec(previewSpec());
}

void ThemeEditor::applyPresetIndex(int index)
{
    const QString id = presetCombo_->itemData(index).toString();
    if (id.isEmpty())
        return;   // 自定义
    applyPreset(id, &style_);
    emitChanged();
}

void ThemeEditor::pickColor(int index)
{
    const QString labels[4] = {tr("主色"), tr("副色"), tr("侧栏主色"), tr("侧栏副色")};
    QColor current;
    switch (index) {
    case 0: current = style_.primary; break;
    case 1: current = style_.secondary; break;
    case 2: current = style_.sidebarStartColor(); break;
    default: current = style_.sidebarEndColor(); break;
    }
    const QColor c = QColorDialog::getColor(current, this, labels[index]);
    if (!c.isValid())
        return;
    switch (index) {
    case 0: style_.primary = c; break;
    case 1: style_.secondary = c; break;
    case 2: style_.sidebarStart = c; break;
    default: style_.sidebarEnd = c; break;
    }
    emitChanged();
}

void ThemeEditor::chooseBackground()
{
    if (imageTask_ && imageTask_->isRunning())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择背景图片"), QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;

    // 大图解码 / 重编码放到后台线程，UI 保持响应
    style_.bgImageName = QFileInfo(path).fileName();
    bgNameLabel_->setText(tr("正在读取图片…"));
    bgNameLabel_->setToolTip(path);
    bgChooseBtn_->setEnabled(false);
    bgClearBtn_->setEnabled(false);
    imageTask_ = new BackgroundImageTask(path, kMaxBackgroundWidth, this);
    connect(imageTask_, &BackgroundImageTask::loaded, this, &ThemeEditor::onBackgroundLoaded);
    connect(imageTask_, &BackgroundImageTask::failed, this, &ThemeEditor::onBackgroundFailed);
    imageTask_->start();
}

void ThemeEditor::onBackgroundLoaded(const QByteArray &encoded, const QSize &pixelSize)
{
    style_.bgImage = encoded;
    const QString info = tr("%1（%2×%3，%4 KB）")
                             .arg(style_.bgImageName)
                             .arg(pixelSize.width())
                             .arg(pixelSize.height())
                             .arg(encoded.size() / 1024.0, 0, 'f', 1);
    bgNameLabel_->setText(info);
    bgNameLabel_->setToolTip(info);
    bgChooseBtn_->setEnabled(true);
    bgClearBtn_->setEnabled(true);
    if (imageTask_) {
        imageTask_->wait();
        imageTask_->deleteLater();
        imageTask_ = nullptr;
    }
    emitChanged();
}

void ThemeEditor::onBackgroundFailed(const QString &error)
{
    bgNameLabel_->setText(tr("读取失败"));
    bgNameLabel_->setToolTip(error);
    bgChooseBtn_->setEnabled(true);
    bgClearBtn_->setEnabled(true);
    if (imageTask_) {
        imageTask_->wait();
        imageTask_->deleteLater();
        imageTask_ = nullptr;
    }
}

void ThemeEditor::clearBackground()
{
    style_.bgImage.clear();
    style_.bgImageName.clear();
    bgNameLabel_->setToolTip(QString());
    emitChanged();
}

void ThemeEditor::emitChanged()
{
    refreshUi();
    emit styleChanged();
}

void ThemeEditor::refreshUi()
{
    for (int i = 0; i < 4; ++i) {
        QColor c;
        switch (i) {
        case 0: c = style_.primary; break;
        case 1: c = style_.secondary; break;
        case 2: c = style_.sidebarStartColor(); break;
        default: c = style_.sidebarEndColor(); break;
        }
        styleSwatch(colorButtons_[i], c);
    }

    {
        const QSignalBlocker block(presetCombo_);
        const QString matched = matchPreset(style_);
        int idx = presetCombo_->count() - 1;   // 自定义
        for (int i = 0; i < presetCombo_->count(); ++i) {
            if (!matched.isEmpty() && presetCombo_->itemData(i).toString() == matched) {
                idx = i;
                break;
            }
        }
        presetCombo_->setCurrentIndex(idx);
    }

    const QString bgText = style_.bgImage.isEmpty()
                               ? tr("未选择")
                               : (style_.bgImageName.isEmpty() ? tr("已选择") : style_.bgImageName);
    bgNameLabel_->setText(bgText);
    bgNameLabel_->setToolTip(style_.bgImage.isEmpty() ? QString() : bgText);
    bgClearBtn_->setEnabled(!style_.bgImage.isEmpty());

    {
        const QSignalBlocker block(fitCombo_);
        const QByteArray fit = bgFitToString(style_.bgFit).toLatin1();
        for (int i = 0; i < fitCombo_->count(); ++i) {
            if (fitCombo_->itemData(i).toByteArray() == fit) {
                fitCombo_->setCurrentIndex(i);
                break;
            }
        }
    }
    {
        const QSignalBlocker block(opacitySlider_);
        opacitySlider_->setValue(style_.bgOpacity);
        opacityValue_->setText(QStringLiteral("%1%").arg(style_.bgOpacity));
    }

    preview_->setTheme(style_);
    preview_->setSpec(previewSpec());
}

QString ThemeEditor::mockTitle() const
{
    const QString app = appName_.isEmpty() ? tr("示例程序") : appName_;
    return uninstaller_ ? tr("卸载 · %1").arg(app) : tr("安装向导 · %1").arg(app);
}

MockSpec ThemeEditor::previewSpec() const
{
    MockSpec spec;
    spec.appName = appName_.isEmpty() ? tr("示例程序") : appName_;
    spec.title = mockTitle();
    spec.uninstaller = uninstaller_;
    spec.subtitle = uninstaller_ ? tr("卸载向导") : tr("安装向导");
    spec.primaryButton = uninstaller_ ? tr("卸载") : tr("安装");
    spec.backButton = tr("上一步");
    spec.cancelButton = tr("取消");
    spec.activeStep = 0;
    spec.steps = uninstaller_
                     ? QStringList{tr("确认卸载"), tr("正在卸载"), tr("完成")}
                     : QStringList{tr("欢迎"), tr("许可协议"), tr("安装选项"),
                                   tr("正在安装"), tr("安装完成")};
    return spec;
}
