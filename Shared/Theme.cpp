#include "Theme.h"

#include "ThemeSpec.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QLabel>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QApplication>
#include <QWindow>
#include <QStyle>
#include <QTranslator>
#include <QLocale>
#include <QFile>
#include <QHash>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace esp {

namespace {

constexpr int kCardRadius = 14;

QString rgb(const QColor &c)
{
    return c.name(QColor::HexRgb);
}

QString rgba(const QColor &c, int alpha)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

// 简单的 {{token}} 替换（QSS 里不能用 CSS 变量，这里编译期生成）
QString applyTokens(QString sheet, const QHash<QString, QString> &tokens)
{
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it) {
        const QString key = QStringLiteral("{{") + it.key() + QStringLiteral("}}");
        if (!sheet.contains(key)) {
            qWarning("globalStyleSheet: 未使用的占位符 %s", qPrintable(key));
            continue;
        }
        sheet.replace(key, it.value());
    }
    if (sheet.contains(QLatin1String("{{")))
        qWarning("globalStyleSheet: 存在未替换的占位符");
    return sheet;
}

} // namespace

void installTranslations(QApplication &app)
{
    // ESP_LANG 环境变量可强制语言（如 ESP_LANG=ja），否则自动识别系统语言
    QStringList langs;
    const QByteArray force = qgetenv("ESP_LANG");
    if (!force.isEmpty())
        langs << QString::fromLatin1(force);
    else
        langs = QLocale::system().uiLanguages();

    // 直接按字符串匹配内嵌 qm（BCP47 归一化：ja-JP → ja_JP，再退回纯语言 ja）
    QString lang;
    const auto tryLoad = [&lang](const QString &name) {
        if (QFile::exists(QStringLiteral(":/i18n/esp_%1.qm").arg(name))) {
            lang = name;
            return true;
        }
        return false;
    };
    for (const QString &l : langs) {
        QString norm = l;
        norm.replace(QLatin1Char('-'), QLatin1Char('_'));
        if (tryLoad(norm))
            break;
        const QString langOnly = norm.section(QLatin1Char('_'), 0, 0);
        if (langOnly != norm && tryLoad(langOnly))
            break;
    }
    if (lang.isEmpty())
        lang = QStringLiteral("en");
    QLocale::setDefault(QLocale(lang));

    auto *espTr = new QTranslator(&app);
    if (espTr->load(QStringLiteral(":/i18n/esp_%1").arg(lang)))
        app.installTranslator(espTr);

    // Qt 标准控件翻译（QMessageBox 按钮、文件对话框等）
    auto *qtTr = new QTranslator(&app);
    if (qtTr->load(QStringLiteral(":/i18n/qtbase_%1").arg(lang)))
        app.installTranslator(qtTr);
}

QString globalStyleSheet(const ThemeStyle &theme)
{
    const QColor primary = theme.primary;
    const QColor secondary = theme.secondary;
    const QColor sbStart = theme.sidebarStartColor();
    const QColor sbEnd = theme.sidebarEndColor();
    const int sbAlpha = theme.sidebarAlpha();

    QHash<QString, QString> tokens;
    tokens["primary"] = rgb(primary);
    tokens["secondary"] = rgb(secondary);
    tokens["primaryHover"] = rgb(primary.darker(114));
    tokens["primaryPressed"] = rgb(primary.darker(132));
    tokens["primaryDisabled"] = rgb(primary.lighter(172));
    tokens["secondaryHover"] = rgb(secondary.darker(114));
    tokens["secondaryPressed"] = rgb(secondary.darker(132));
    tokens["sidebarStart"] = rgba(sbStart, sbAlpha);
    tokens["sidebarEnd"] = rgba(sbEnd, sbAlpha);
    tokens["primaryHoverBg"] = rgb(primary.lighter(188));

    QString sheet = QStringLiteral(R"(
* { font-family: "Segoe UI", "Microsoft YaHei UI", "Microsoft YaHei", sans-serif; }
QWidget { color: #1F2430; }

/* ---------- 主按钮 ---------- */
QPushButton[role="primary"] {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {{primary}}, stop:1 {{secondary}});
    color: white; border: none; border-radius: 8px;
    padding: 9px 22px; font-size: 13px; font-weight: 600;
}
QPushButton[role="primary"]:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {{primaryHover}}, stop:1 {{secondaryHover}});
}
QPushButton[role="primary"]:pressed {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {{primaryPressed}}, stop:1 {{secondaryPressed}});
}
QPushButton[role="primary"]:disabled {
    background: {{primaryDisabled}}; color: #F1F3FB;
}

/* ---------- 次级按钮 ---------- */
QPushButton[role="secondary"] {
    background: white; color: #1F2430; border: 1px solid #D6DCE8;
    border-radius: 8px; padding: 8px 20px; font-size: 13px;
}
QPushButton[role="secondary"]:hover { background: #F1F4FA; border-color: #B9C4D8; }
QPushButton[role="secondary"]:pressed { background: #E7ECF5; }
QPushButton[role="secondary"]:disabled { color: #A8B0C0; border-color: #E3E8F2; background: #F7F9FC; }

/* ---------- 危险按钮 ---------- */
QPushButton[role="danger"] {
    background: #E5484D; color: white; border: none; border-radius: 8px;
    padding: 9px 22px; font-size: 13px; font-weight: 600;
}
QPushButton[role="danger"]:hover { background: #D63C41; }
QPushButton[role="danger"]:pressed { background: #C23438; }
QPushButton[role="danger"]:disabled { background: #F2B4B6; color: #FDEBEC; }

/* ---------- 链接样式按钮 ---------- */
QPushButton[role="link"] {
    background: transparent; color: {{primary}}; border: none; font-size: 13px;
}
QPushButton[role="link"]:hover { color: {{secondary}}; text-decoration: underline; }

/* ---------- 输入框 ---------- */
QLineEdit {
    background: white; border: 1px solid #D6DCE8; border-radius: 8px;
    padding: 8px 12px; font-size: 13px; selection-background-color: {{primary}};
}
QLineEdit:focus { border: 1px solid {{primary}}; }
QLineEdit:disabled { background: #F3F5F9; color: #9AA3B5; }

/* ---------- 下拉框 ---------- */
QComboBox {
    background: white; border: 1px solid #D6DCE8; border-radius: 8px;
    padding: 6px 10px; font-size: 13px; min-height: 22px;
}
QComboBox:focus { border: 1px solid {{primary}}; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: white; border: 1px solid #D6DCE8; border-radius: 8px;
    selection-background-color: {{primaryHoverBg}}; selection-color: #1F2430; outline: none;
}

/* ---------- 滑块 ---------- */
QSlider::groove:horizontal {
    height: 6px; border-radius: 3px; background: #E8EDF6;
}
QSlider::sub-page:horizontal {
    height: 6px; border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {{primary}}, stop:1 {{secondary}});
}
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -5px 0; border-radius: 7px;
    background: white; border: 2px solid {{primary}};
}

/* ---------- 复选框 ---------- */
QCheckBox { font-size: 13px; color: #1F2430; spacing: 8px; }
QCheckBox::indicator { width: 17px; height: 17px; border-radius: 5px; border: 1px solid #C4CDDE; background: white; }
QCheckBox::indicator:hover { border-color: {{primary}}; }
QCheckBox::indicator:checked { background: {{primary}}; border-color: {{primary}}; }
QCheckBox::indicator:disabled { background: #EDF0F6; border-color: #DDE3EE; }

/* ---------- 进度条 ---------- */
QProgressBar {
    background: #E8EDF6; border: none; border-radius: 6px; height: 8px;
    text-align: center; font-size: 0px;
}
QProgressBar::chunk {
    border-radius: 6px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 {{primary}}, stop:1 {{secondary}});
}

/* ---------- 日志区 ---------- */
QPlainTextEdit[role="log"] {
    background: #FBFCFE; border: 1px solid #E3E8F2; border-radius: 8px;
    font-family: "Consolas", "Courier New", monospace; font-size: 12px; color: #3A4356;
}
QTextEdit[role="license"] {
    background: #FBFCFE; border: 1px solid #E3E8F2; border-radius: 8px;
    padding: 10px; font-size: 12px; color: #3A4356;
}

/* ---------- 选项卡 ---------- */
QTabWidget::pane { border: 1px solid #E3E8F2; border-radius: 10px; background: #FBFCFE; top: -1px; }
QTabBar::tab {
    background: transparent; color: #5A6478; padding: 8px 18px; margin-right: 4px;
    border: 1px solid transparent; border-top-left-radius: 8px; border-top-right-radius: 8px;
    font-size: 13px;
}
QTabBar::tab:selected { background: #FBFCFE; color: #1F2430; border-color: #E3E8F2; font-weight: 600; }
QTabBar::tab:hover:!selected { color: {{primary}}; }

/* ---------- 侧栏 ---------- */
QWidget[role="sidebar"] {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 {{sidebarStart}}, stop:1 {{sidebarEnd}});
    border-bottom-left-radius: 14px;
}
QLabel[role="sidebarTitle"] { color: white; font-size: 15px; font-weight: 700; }
QLabel[role="sidebarSub"] { color: rgba(255,255,255,0.82); font-size: 12px; }
QLabel[role="sidebarStep"] { color: rgba(255,255,255,0.6); font-size: 12.5px; }
QLabel[role="sidebarStepActive"] { color: white; font-size: 12.5px; font-weight: 600; }

/* ---------- 大标题/描述 ---------- */
QLabel[role="pageTitle"] { font-size: 19px; font-weight: 700; color: #1F2430; }
QLabel[role="pageSub"] { font-size: 13px; color: #6B7280; }
QLabel[role="fieldLabel"] { font-size: 12.5px; font-weight: 600; color: #454E62; }

/* ---------- 标题栏 ---------- */
QLabel[role="titleText"] { font-size: 13px; font-weight: 600; color: #2A3245; }
QFrame[role="titleSep"] { background: #ECF0F7; }
QToolButton[role="titlebtn"] {
    background: transparent; border: none; border-radius: 6px; color: #5A6478;
    font-family: "Segoe UI"; font-size: 13px; font-weight: 400;
}
QToolButton[role="titlebtn"]:hover { background: rgba(31,36,48,0.08); }
QToolButton[role2="titlebtnClose"]:hover { background: #E5484D; color: white; }

/* ---------- 卡片 / 色块 ---------- */
QFrame[role="fieldCard"], QWidget[role="fieldCard"] {
    background: #FBFCFE; border: 1px solid #E3E8F2; border-radius: 10px;
}
QPushButton[role="swatch"] {
    border: 1px solid #D6DCE8; border-radius: 8px; min-height: 30px;
    font-family: "Consolas", monospace; font-size: 11px;
}

/* ---------- 单选按钮 ---------- */
QRadioButton { font-size: 13px; spacing: 8px; }
QRadioButton::indicator { width: 17px; height: 17px; border-radius: 9px; border: 1px solid #C4CDDE; background: white; }
QRadioButton::indicator:hover { border-color: {{primary}}; }
QRadioButton::indicator:checked { border: 5px solid {{primary}}; background: white; }
)");
    return applyTokens(sheet, tokens);
}

QString bottomBarStyle(const ThemeStyle &theme)
{
    const QColor base(0xFA, 0xFB, 0xFE);
    return QStringLiteral(
               "QWidget#btnBar { background: %1; border-top: 1px solid #ECF0F7;"
               " border-bottom-right-radius: 14px; }")
        .arg(theme.hasBackground() ? rgba(base, theme.barAlpha()) : rgb(base));
}

QPixmap appPixmap(int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const qreal s = size / 64.0;
    QLinearGradient g(0, 0, 0, size);
    g.setColorAt(0, QColor(0x4F, 0x6D, 0xF5));
    g.setColorAt(1, QColor(0x7C, 0x3A, 0xED));

    // 圆角方箱
    QRectF box(8 * s, 20 * s, 48 * s, 36 * s);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(box, 10 * s, 10 * s);

    // 箱盖
    QRectF lid(6 * s, 14 * s, 52 * s, 10 * s);
    p.drawRoundedRect(lid, 5 * s, 5 * s);

    // 白色上箭头
    QPainterPath arrow;
    const qreal cx = 32 * s;
    arrow.moveTo(cx, 22 * s);
    arrow.lineTo(cx + 9 * s, 31 * s);
    arrow.lineTo(cx + 3.5 * s, 31 * s);
    arrow.lineTo(cx + 3.5 * s, 40 * s);
    arrow.lineTo(cx - 3.5 * s, 40 * s);
    arrow.lineTo(cx - 3.5 * s, 31 * s);
    arrow.lineTo(cx - 9 * s, 31 * s);
    arrow.closeSubpath();
    p.setBrush(Qt::white);
    p.drawPath(arrow);
    return pm;
}

QIcon appIcon(int size)
{
    return QIcon(appPixmap(size));
}

// =====================================================================
// BackdropCard — 圆角卡片 + 主题背景图
// =====================================================================

class BackdropCard : public QWidget
{
public:
    explicit BackdropCard(QWidget *parent = nullptr) : QWidget(parent) {}

    void setBackground(const ThemeStyle &theme, const QImage &image)
    {
        theme_ = theme;
        image_ = image;
        scaled_ = QPixmap();
        scaledFor_ = QSize();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        // Cover 是最常用且最费时的情形：按目标尺寸预缩放并缓存，
        // 之后每帧只是一次 1:1 贴图，避免频繁重绘时重复平滑缩放造成卡顿。
        QImage image = image_;
        if (!image_.isNull() && theme_.bgFit == BgFit::Cover
            && image_.width() > width() && image_.height() > height()) {
            if (scaled_.isNull() || scaledFor_ != size()) {
                const QSize target = image_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
                scaled_ = QPixmap::fromImage(
                    image_.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
                scaledFor_ = size();
            }
            image = scaled_.toImage();
        }
        QPainter p(this);
        paintBackground(&p, rect(), theme_, image, QColor(Qt::white), kCardRadius);
    }

private:
    ThemeStyle theme_;
    QImage image_;
    QPixmap scaled_;
    QSize scaledFor_;
};

// =====================================================================
// TitleBar
// =====================================================================

TitleBar::TitleBar(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(46);
    setCursor(Qt::ArrowCursor);

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(18, 0, 12, 0);
    lay->setSpacing(10);

    auto *icon = new QLabel(this);
    icon->setPixmap(appPixmap(22));
    icon->setObjectName(QStringLiteral("titleIcon"));
    lay->addWidget(icon);

    auto *titleLabel = new QLabel(title, this);
    titleLabel->setProperty("role", "titleText");
    lay->addWidget(titleLabel);
    lay->addStretch();

    buttons_ = new QWidget(this);
    auto *blay = new QHBoxLayout(buttons_);
    blay->setContentsMargins(0, 0, 0, 0);
    blay->setSpacing(4);

    auto *minBtn = new QToolButton(buttons_);
    minBtn->setProperty("role", "titlebtn");
    minBtn->setText(QStringLiteral("—"));
    minBtn->setFixedSize(36, 30);
    minBtn->setToolTip(tr("最小化"));
    connect(minBtn, &QToolButton::clicked, this, &TitleBar::minimizeClicked);
    blay->addWidget(minBtn);

    auto *closeBtn = new QToolButton(buttons_);
    closeBtn->setProperty("role", "titlebtn");
    closeBtn->setProperty("role2", "titlebtnClose");
    closeBtn->setText(QStringLiteral("✕"));
    closeBtn->setFixedSize(36, 30);
    closeBtn->setToolTip(tr("关闭"));
    connect(closeBtn, &QToolButton::clicked, this, &TitleBar::closeClicked);
    blay->addWidget(closeBtn);

    lay->addWidget(buttons_);
}

void TitleBar::setIcon(const QPixmap &pm)
{
    auto *icon = findChild<QLabel *>(QStringLiteral("titleIcon"));
    if (!icon || pm.isNull())
        return;
    icon->setPixmap(pm.scaled(22, 22, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void TitleBar::mousePressEvent(QMouseEvent *e)
{
    // 拖拽全部交给系统 startSystemMove；必须 accept 阻断事件传播，
    // 否则事件冒泡到父窗口的旧手动拖拽逻辑会导致窗口被"粘住"随鼠标漂移。
    if (e->button() == Qt::LeftButton) {
        if (window() && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (window() && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

// =====================================================================
// ModernWindow
// =====================================================================

ModernWindow::ModernWindow(const QString &title, const QSize &size,
                           QWidget *sidebar, QWidget *parent)
    : QWidget(parent), title_(title)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowTitle(title);
    setWindowIcon(appIcon(32));
    resize(size);
    setMinimumSize(560, 380);

    // 任务栏按钮：包装模式下本窗口是 owned window（有父窗口），Windows 默认不显示
    // 任务栏按钮，显式加 WS_EX_APPWINDOW 保证任务栏显示图标+标签。
    {
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_APPWINDOW);
    }

    buildUi();
    if (sidebar)
        sidebarSlot_->layout()->addWidget(sidebar);
}

void ModernWindow::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(22, 22, 22, 22);

    card_ = new BackdropCard(this);
    card_->setObjectName(QStringLiteral("card"));
    outer->addWidget(card_);

    auto *shadow = new QGraphicsDropShadowEffect(card_);
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(15, 23, 42, 70));
    card_->setGraphicsEffect(shadow);

    auto *cardLay = new QVBoxLayout(card_);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    auto *bar = new TitleBar(title_, card_);
    bar_ = bar;
    connect(bar, &TitleBar::minimizeClicked, this, [this] { showMinimized(); });
    // 关闭按钮只发信号，由包装类决定是否关闭（安装向导需要确认）
    connect(bar, &TitleBar::closeClicked, this, [this] { emit closeClicked(); });
    titleBarButtons_ = bar->buttons();
    cardLay->addWidget(bar);

    auto *sep = new QFrame(card_);
    sep->setProperty("role", "titleSep");
    sep->setFixedHeight(1);
    cardLay->addWidget(sep);

    auto *body = new QWidget(card_);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);
    cardLay->addWidget(body, 1);

    sidebarSlot_ = new QWidget(body);
    auto *sbLay = new QVBoxLayout(sidebarSlot_);
    sbLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->addWidget(sidebarSlot_);

    contentSlot_ = new QWidget(body);
    auto *ctLay = new QVBoxLayout(contentSlot_);
    ctLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->addWidget(contentSlot_, 1);
}

void ModernWindow::setContent(QWidget *content)
{
    while (auto *item = contentSlot_->layout()->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    contentSlot_->layout()->addWidget(content);
}

void ModernWindow::setSidebar(QWidget *sidebar)
{
    if (!sidebar)
        return;
    while (auto *item = sidebarSlot_->layout()->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    sidebarSlot_->layout()->addWidget(sidebar);
}

void ModernWindow::applyTheme(const ThemeStyle &theme)
{
    theme_ = theme;
    // 背景图在此一次性有界解码（≤2048 宽），之后所有重绘直接用缓存，
    // 避免每次 paintEvent 解码大图导致界面未响应。
    bgImage_ = theme.hasBackground() ? decodeBackground(theme.bgImage, 2048) : QImage();
    if (card_)
        card_->setBackground(theme_, bgImage_);
}

void ModernWindow::setFixedClientSize(int w, int h)
{
    setFixedSize(w + 44, h + 46 + 1); // 阴影边距 + 标题栏 + 分隔线
}

void ModernWindow::setTitleBarIcon(const QPixmap &pm)
{
    if (bar_)
        bar_->setIcon(pm);
}

void ModernWindow::setClosable(bool closable)
{
    closable_ = closable;
}

void ModernWindow::closeEvent(QCloseEvent *e)
{
    // 任务进行中（打包/安装/卸载）禁止关闭，避免线程未结束导致退出崩溃
    if (!closable_) {
        e->ignore();
        return;
    }
    e->accept();
    // wrapper 模式下（包装 QWidget 无窗口标志）lastWindowClosed 不会触发，
    // 必须显式 quit，否则窗口隐藏后进程常驻。
    QCoreApplication::quit();
}

} // namespace esp
