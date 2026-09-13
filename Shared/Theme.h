#pragma once
// Theme — 三件套统一视觉：无边框圆角窗口基类 + 现代 QSS + 程序图标

#include <QWidget>
#include <QString>
#include <QSize>
#include <QIcon>

class QApplication;

namespace esp {

// 按系统语言（或环境变量 ESP_LANG）加载内嵌翻译（esp_*.qm + qtbase_*.qm）
void installTranslations(QApplication &app);

// ---------- 配色 ----------
constexpr const char *kColorAccent1 = "#4F6DF5"; // 主蓝
constexpr const char *kColorAccent2 = "#8B5CF6"; // 渐变紫
constexpr const char *kColorText = "#1F2430";
constexpr const char *kColorMuted = "#6B7280";
constexpr const char *kColorBg = "#F5F7FB";
constexpr const char *kColorDanger = "#E5484D";
constexpr const char *kColorOk = "#18A058";

// 全局 QSS（浅色现代扁平风）
QString globalStyleSheet();

// 程序图标（QPainter 绘制：渐变圆角箱 + 上箭头），尺寸 size
QIcon appIcon(int size = 64);
QPixmap appPixmap(int size = 64);

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

    void setFixedClientSize(int w, int h);
    void setClosable(bool closable);

signals:
    void closeClicked();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void buildUi();

    QWidget *card_ = nullptr;         // 圆角卡片（含阴影）
    QWidget *sidebarSlot_ = nullptr;  // 侧栏占位容器
    QWidget *contentSlot_ = nullptr;  // 内容占位容器
    QWidget *titleBarButtons_ = nullptr;
    class TitleBar *bar_ = nullptr;
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
