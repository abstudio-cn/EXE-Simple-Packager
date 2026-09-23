#pragma once
// ThemeSpec — 可自定义外观（配色 + 背景图片），安装器 / 卸载器各一套
//
// 设计要点：
//  * ThemeStyle 只保存「已编码」的背景图字节（QByteArray），不持有 QImage/QPixmap，
//    因此可以在工作线程之间安全复制，也能直接序列化进封包索引与 uninstall.ini。
//  * 背景图解码一律「有界」：按目标宽度用 QImageReader::setScaledSize 解码，
//    避免超大图片在 UI 线程整图解码造成界面未响应。
//  * 绘制统一走 paintBackground()，打包器预览与运行时窗口共用同一份实现，保证 WYSIWYG。

#include <QByteArray>
#include <QColor>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QImage;
class QJsonObject;
class QPainter;
class QRect;

namespace esp {

class IniFile;

// 背景图填充方式
enum class BgFit {
    Cover,    // 等比铺满（裁剪超出部分）—— 默认
    Contain,  // 等比适应（留边）
    Stretch,  // 拉伸铺满（不等比）
    Tile,     // 原始尺寸平铺
    Center    // 原始尺寸居中
};

QString bgFitToString(BgFit fit);
BgFit bgFitFromString(const QString &s);

// 一套外观主题
struct ThemeStyle
{
    QColor primary   = QColor(0x4F, 0x6D, 0xF5); // 主色：按钮 / 进度 / 焦点 / 复选框
    QColor secondary = QColor(0x8B, 0x5C, 0xF6); // 副色：渐变终点
    QColor sidebarStart;                          // 侧栏渐变起始（无效 → primary）
    QColor sidebarEnd;                            // 侧栏渐变终点（无效 → secondary）

    QByteArray bgImage;                 // 背景图编码字节（PNG/JPEG），空 = 无背景图
    QString bgImageName;                // 背景图原始文件名（仅界面展示用）
    BgFit bgFit = BgFit::Cover;
    int bgOpacity = 100;                // 背景图不透明度 0-100

    QColor sidebarStartColor() const { return sidebarStart.isValid() ? sidebarStart : primary; }
    QColor sidebarEndColor() const { return sidebarEnd.isValid() ? sidebarEnd : secondary; }
    bool hasBackground() const { return !bgImage.isEmpty(); }

    // 侧栏 / 底栏在存在背景图时使用半透明，让背景图透出（与预览绘制保持一致）
    int sidebarAlpha() const { return hasBackground() ? 214 : 255; }   // 0.84
    int barAlpha() const { return hasBackground() ? 226 : 255; }       // 0.89
};

// 安装器 + 卸载器两套主题
struct ThemePack
{
    ThemeStyle installer;
    ThemeStyle uninstaller;
};

// ---------------- 预设配色 ----------------
struct ThemePreset
{
    const char *id;        // 语言无关 id（CLI 用）
    const char *nameKey;   // 翻译上下文 key（ThemeSpec）
    const char *primary;
    const char *secondary;
    const char *sidebarStart;
    const char *sidebarEnd;
};

const QVector<ThemePreset> &themePresets();
const ThemePreset *findPreset(const QString &id);

// 预设的本地化显示名
QString presetDisplayName(const ThemePreset &p);

// 应用预设配色（不改动背景图设置）。返回 false 表示 id 未知。
bool applyPreset(const QString &id, ThemeStyle *style);

// 判断当前配色匹配哪个预设，返回预设 id；不匹配返回空串。
QString matchPreset(const ThemeStyle &style);

// ---------------- 序列化 ----------------
// 封包索引 JSON（不含背景图字节，背景图存主题块）
QJsonObject themeStyleToJson(const ThemeStyle &s);
bool themeStyleFromJson(const QJsonObject &o, ThemeStyle *s);

// uninstall.ini [Theme] 段
void writeThemeToIni(IniFile *ini, const ThemeStyle &s, const QString &bgFileName);
// 读取 [Theme] 段；bgFileName 返回相对安装目录的图片文件名（可能为空）
bool readThemeFromIni(const IniFile &ini, ThemeStyle *s, QString *bgFileName);

// 从 uninstall.ini 读取卸载器外观（配色 + 背景图字节；背景图相对 ini 所在目录解析）。
// 缺少 [Theme] 段或图片缺失时回退（返回默认/纯色外观）。
ThemeStyle loadUninstallTheme(const QString &iniPath, QString *bgFileName = nullptr);

// ---------------- 背景图 ----------------
// 从文件读取图片 → 有界重编码（PNG / JPEG 取更小者）。
// maxWidth > 0 时按比例缩放到不超过该宽度。成功返回 true 并输出编码字节与像素尺寸。
bool encodeBackgroundFile(const QString &filePath, int maxWidth,
                          QByteArray *encoded, QSize *pixelSize, QString *error);

// 从编码字节解码为 QImage（有界：maxWidth 限制最大宽度，0 = 不限制）。
// 失败返回空 QImage。
QImage decodeBackground(const QByteArray &bytes, int maxWidth = 2048);

// 背景绘制（唯一实现，预览与运行时共用）。
// 先用 baseColor 铺底，再按 bgFit/bgOpacity 画图。radius > 0 时按圆角矩形裁剪。
void paintBackground(QPainter *p, const QRect &rect, const ThemeStyle &style,
                     const QImage &image, const QColor &baseColor, int radius = 0);

} // namespace esp
