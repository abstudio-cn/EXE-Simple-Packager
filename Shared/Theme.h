#pragma once
// Theme — 三件套统一视觉：无边框圆角窗口基类 + 主题化 QSS + 程序图标
//
// 主题（配色 + 背景图）由 ThemeSpec 定义，安装器 / 卸载器可各自不同。

#include "ThemeSpec.h"

#include <QIcon>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <QWidget>

class QApplication;

namespace esp {

// 按系统语言（或环境变量 ESP_LANG）加载内嵌翻译（esp_*.qm + qtbase_*.qm）
void installTranslations(QApplication &app);

// ---------- 语义色（不随主题变化） ----------
constexpr const char *kColorDanger = "#E5484D";
constexpr const char *kColorOk = "#18A058";

// 全局 QSS（由主题配色生成：主/次色 → 按钮、进度、焦点、复选框、侧栏渐变）
QString globalStyleSheet(const ThemeStyle &theme);

// 底部按钮栏样式（内容列底栏；存在背景图时半透明并保留右下圆角）
QString bottomBarStyle(const ThemeStyle &theme);

// 程序图标（QPainter 绘制：渐变圆角箱 + 上箭头），尺寸 size
QIcon appIcon(int size = 64);
QPixmap appPixmap(int size = 64);

// 卡片容器：绘制圆角白底 + 主题背景图（整卡铺满，内部控件半透明处可透出）
class BackdropCard;

// ---------- 无边框圆角窗口 ----------
// 用法：子类构造后调用 setContent()/setSidebar()；标题栏自动含 logo + 标题 + 最小化/关闭。
class ModernWindow : public QWidget
{
    Q_OBJECT
public:
    // sidebarWidth > 0 时左侧显示渐变品牌侧栏（传入侧栏 widget）
    explicit ModernWindow(const QString &title, const QSize &size,
                          QWidget *sidebar = nullptr, QWidget *parent = nullptr);

    // 设置标题栏右侧追加的自定义按钮（返回容器）
    QWidget *titleBarButtons() { return titleBarButtons_; }

    // 设置标题栏 logo 图标（默认使用内置 appPixmap）
    void setTitleBarIcon(const QPixmap &pm);

    // 设置中间内容区
    void setContent(QWidget *content);

    // 设置左侧渐变侧栏（可后于构造调用）
    void setSidebar(QWidget *sidebar);

    // 应用主题（配色 + 背景图）。背景图在此处一次性有界解码并缓存，避免重绘时解码卡顿。
    void applyTheme(const ThemeStyle &theme);
    const ThemeStyle &theme() const { return theme_; }

    void setFixedClientSize(int w, int h);
    void setClosable(bool closable);

signals:
    void closeClicked();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildUi();

    BackdropCard *card_ = nullptr;   // 圆角卡片（含阴影、背景图）
    QWidget *sidebarSlot_ = nullptr; // 侧栏占位容器
    QWidget *contentSlot_ = nullptr; // 内容占位容器
    QWidget *titleBarButtons_ = nullptr;
    class TitleBar *bar_ = nullptr;
    ThemeStyle theme_;
    QImage bgImage_;
    bool closable_ = true;
    QString title_;
};

// 标题栏中的拖拽 + 最小化/关闭按钮（内部使用）
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    TitleBar(const QString &title, QWidget *parent = nullptr);
    QWidget *buttons() { return buttons_; }
    void setIcon(const QPixmap &pm);

signals:
    void minimizeClicked();
    void closeClicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    QWidget *buttons_ = nullptr;
};

} // namespace esp
