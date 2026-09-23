#include "ThemeSpec.h"

#include "IniFile.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QBrush>
#include <QStringList>

namespace esp {

namespace {
// i18n：静态库无 QObject 上下文，统一用 translate
QString tr_(const char *s)
{
    return QCoreApplication::translate("ThemeSpec", s);
}

QString colorName(const QColor &c)
{
    return c.isValid() ? c.name(QColor::HexRgb) : QString();
}

QByteArray encodeImage(const QImage &img, const char *format, int quality)
{
    QByteArray out;
    QBuffer buf(&out);
    if (!buf.open(QIODevice::WriteOnly))
        return {};
    QImageWriter w(&buf, QByteArray(format));
    if (quality > 0)
        w.setQuality(quality);
    if (!w.write(img))
        return {};
    buf.close();
    return out;
}
} // namespace

// =====================================================================
// 背景图填充方式
// =====================================================================

QString bgFitToString(BgFit fit)
{
    switch (fit) {
    case BgFit::Cover:   return QStringLiteral("cover");
    case BgFit::Contain: return QStringLiteral("contain");
    case BgFit::Stretch: return QStringLiteral("stretch");
    case BgFit::Tile:    return QStringLiteral("tile");
    case BgFit::Center:  return QStringLiteral("center");
    }
    return QStringLiteral("cover");
}

BgFit bgFitFromString(const QString &s)
{
    const QString v = s.trimmed().toLower();
    if (v == QLatin1String("contain")) return BgFit::Contain;
    if (v == QLatin1String("stretch")) return BgFit::Stretch;
    if (v == QLatin1String("tile"))    return BgFit::Tile;
    if (v == QLatin1String("center"))  return BgFit::Center;
    return BgFit::Cover;
}

// =====================================================================
// 预设配色
// =====================================================================

const QVector<ThemePreset> &themePresets()
{
    // QT_TRANSLATE_NOOP 让 lupdate 能提取名称，运行时再按语言解析
    static const QVector<ThemePreset> presets = {
        {"classic",  QT_TRANSLATE_NOOP("ThemeSpec", "经典蓝紫"), "#4F6DF5", "#8B5CF6", "#3D5AF1", "#7C3AED"},
        {"ocean",    QT_TRANSLATE_NOOP("ThemeSpec", "深海蓝"),   "#0EA5E9", "#2563EB", "#075985", "#1D4ED8"},
        {"forest",   QT_TRANSLATE_NOOP("ThemeSpec", "翠绿"),     "#10B981", "#059669", "#065F46", "#047857"},
        {"sunset",   QT_TRANSLATE_NOOP("ThemeSpec", "落日橙"),   "#F97316", "#EF4444", "#B45309", "#DC2626"},
        {"magenta",  QT_TRANSLATE_NOOP("ThemeSpec", "品红"),     "#EC4899", "#8B5CF6", "#9D174D", "#6D28D9"},
        {"graphite", QT_TRANSLATE_NOOP("ThemeSpec", "石墨灰"),   "#475569", "#1E293B", "#1E293B", "#0F172A"},
        {"rose",     QT_TRANSLATE_NOOP("ThemeSpec", "玫瑰红"),   "#F43F5E", "#BE123C", "#9F1239", "#881337"},
        {"teal",     QT_TRANSLATE_NOOP("ThemeSpec", "青色"),     "#14B8A6", "#0891B2", "#0F766E", "#0E7490"},
    };
    return presets;
}

const ThemePreset *findPreset(const QString &id)
{
    for (const ThemePreset &p : themePresets())
        if (id.compare(QLatin1String(p.id), Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

QString presetDisplayName(const ThemePreset &p)
{
    return QCoreApplication::translate("ThemeSpec", p.nameKey);
}

bool applyPreset(const QString &id, ThemeStyle *style)
{
    const ThemePreset *p = findPreset(id);
    if (!p || !style)
        return false;
    style->primary = QColor(QString::fromLatin1(p->primary));
    style->secondary = QColor(QString::fromLatin1(p->secondary));
    style->sidebarStart = QColor(QString::fromLatin1(p->sidebarStart));
    style->sidebarEnd = QColor(QString::fromLatin1(p->sidebarEnd));
    return true;
}

QString matchPreset(const ThemeStyle &style)
{
    for (const ThemePreset &p : themePresets()) {
        if (style.primary == QColor(QString::fromLatin1(p.primary))
            && style.secondary == QColor(QString::fromLatin1(p.secondary))
            && style.sidebarStartColor() == QColor(QString::fromLatin1(p.sidebarStart))
            && style.sidebarEndColor() == QColor(QString::fromLatin1(p.sidebarEnd)))
            return QString::fromLatin1(p.id);
    }
    return QString();
}

// =====================================================================
// 序列化
// =====================================================================

QJsonObject themeStyleToJson(const ThemeStyle &s)
{
    QJsonObject o;
    o["primary"] = colorName(s.primary);
    o["secondary"] = colorName(s.secondary);
    o["sidebarStart"] = colorName(s.sidebarStartColor());
    o["sidebarEnd"] = colorName(s.sidebarEndColor());
    o["bgFit"] = bgFitToString(s.bgFit);
    o["bgOpacity"] = s.bgOpacity;
    return o;
}

bool themeStyleFromJson(const QJsonObject &o, ThemeStyle *s)
{
    if (!s || o.isEmpty())
        return false;
    const QColor p(o.value("primary").toString());
    const QColor sec(o.value("secondary").toString());
    if (p.isValid())
        s->primary = p;
    if (sec.isValid())
        s->secondary = sec;
    const QColor s1(o.value("sidebarStart").toString());
    const QColor s2(o.value("sidebarEnd").toString());
    s->sidebarStart = s1;   // 无效即为无效（回退到 primary/secondary）
    s->sidebarEnd = s2;
    s->bgFit = bgFitFromString(o.value("bgFit").toString());
    if (o.contains("bgOpacity"))
        s->bgOpacity = qBound(0, o.value("bgOpacity").toInt(100), 100);
    return true;
}

void writeThemeToIni(IniFile *ini, const ThemeStyle &s, const QString &bgFileName)
{
    if (!ini)
        return;
    const QString sec = QStringLiteral("Theme");
    ini->set(sec, QStringLiteral("Primary"), colorName(s.primary));
    ini->set(sec, QStringLiteral("Secondary"), colorName(s.secondary));
    ini->set(sec, QStringLiteral("SidebarStart"), colorName(s.sidebarStartColor()));
    ini->set(sec, QStringLiteral("SidebarEnd"), colorName(s.sidebarEndColor()));
    ini->set(sec, QStringLiteral("BgImage"), bgFileName);
    ini->set(sec, QStringLiteral("BgFit"), bgFitToString(s.bgFit));
    ini->set(sec, QStringLiteral("BgOpacity"), QString::number(qBound(0, s.bgOpacity, 100)));
}

bool readThemeFromIni(const IniFile &ini, ThemeStyle *s, QString *bgFileName)
{
    if (!s)
        return false;
    const QString sec = QStringLiteral("Theme");
    const QString p = ini.get(sec, QStringLiteral("Primary"));
    if (p.isEmpty())
        return false;   // 无主题段 → 使用默认外观
    const QColor pc(p);
    if (pc.isValid())
        s->primary = pc;
    const QColor sc(ini.get(sec, QStringLiteral("Secondary")));
    if (sc.isValid())
        s->secondary = sc;
    const QColor s1(ini.get(sec, QStringLiteral("SidebarStart")));
    const QColor s2(ini.get(sec, QStringLiteral("SidebarEnd")));
    s->sidebarStart = s1;
    s->sidebarEnd = s2;
    s->bgFit = bgFitFromString(ini.get(sec, QStringLiteral("BgFit")));
    bool ok = false;
    const int op = ini.get(sec, QStringLiteral("BgOpacity")).toInt(&ok);
    s->bgOpacity = ok ? qBound(0, op, 100) : 100;
    if (bgFileName)
        *bgFileName = ini.get(sec, QStringLiteral("BgImage"));
    return true;
}

ThemeStyle loadUninstallTheme(const QString &iniPath, QString *bgFileName)
{
    ThemeStyle style;
    QString bgName;
    const IniFile ini(iniPath);
    if (!readThemeFromIni(ini, &style, &bgName)) {
        if (bgFileName)
            bgFileName->clear();
        return style;   // 无主题配置 → 默认外观
    }
    if (!bgName.isEmpty()) {
        // 背景图存放在安装目录（与 uninstall.ini 同目录）
        QFile f(QDir(QFileInfo(iniPath).absolutePath()).filePath(bgName));
        if (f.open(QIODevice::ReadOnly))
            style.bgImage = f.readAll();
        else
            bgName.clear();   // 图片缺失 → 纯色主题，不影响卸载
        style.bgImageName = bgName;
    }
    if (bgFileName)
        *bgFileName = bgName;
    return style;
}

// =====================================================================
// 背景图
// =====================================================================

bool encodeBackgroundFile(const QString &filePath, int maxWidth,
                          QByteArray *encoded, QSize *pixelSize, QString *error)
{
    const auto fail = [&](const QString &msg) {
        if (error) *error = msg;
        return false;
    };

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return fail(tr_("无法读取图片：%1（%2）").arg(filePath, reader.errorString()));

    // 有界解码：直接让解码器按目标尺寸缩放，避免整图解码（大图会明显卡顿）
    const QSize orig = reader.size();
    if (orig.isValid() && maxWidth > 0 && orig.width() > maxWidth) {
        const int h = qMax(1, int(qint64(orig.height()) * maxWidth / qMax(1, orig.width())));
        reader.setScaledSize(QSize(maxWidth, h));
    }
    const QImage img = reader.read();
    if (img.isNull())
        return fail(tr_("图片解码失败：%1（%2）").arg(filePath, reader.errorString()));
    if (pixelSize)
        *pixelSize = img.size();

    // 编码：PNG 保真（支持透明）；无 alpha 时同时试 JPEG 取更小者，控制安装包体积
    QByteArray best = encodeImage(img, "png", -1);
    if (!img.hasAlphaChannel()) {
        const QByteArray jpg = encodeImage(img.convertToFormat(QImage::Format_RGB888), "jpg", 88);
        if (!jpg.isEmpty() && (best.isEmpty() || jpg.size() < best.size()))
            best = jpg;
    }
    if (best.isEmpty())
        return fail(tr_("图片编码失败：%1").arg(filePath));
    if (encoded)
        *encoded = best;
    return true;
}

QImage decodeBackground(const QByteArray &bytes, int maxWidth)
{
    if (bytes.isEmpty())
        return {};
    QBuffer buf;
    buf.setData(bytes);
    if (!buf.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buf);
    reader.setAutoTransform(true);
    const QSize orig = reader.size();
    if (orig.isValid() && maxWidth > 0 && orig.width() > maxWidth) {
        const int h = qMax(1, int(qint64(orig.height()) * maxWidth / qMax(1, orig.width())));
        reader.setScaledSize(QSize(maxWidth, h));
    }
    return reader.read();
}

void paintBackground(QPainter *p, const QRect &rect, const ThemeStyle &style,
                     const QImage &image, const QColor &baseColor, int radius)
{
    if (!p || rect.isEmpty())
        return;

    p->save();
    if (radius > 0) {
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect), radius, radius);
        p->setClipPath(clip);
    }
    p->fillRect(rect, baseColor);

    if (!image.isNull() && style.bgOpacity > 0) {
        p->setRenderHint(QPainter::SmoothPixmapTransform, true);
        p->setOpacity(qBound(0, style.bgOpacity, 100) / 100.0);
        const QSize isz = image.size();
        switch (style.bgFit) {
        case BgFit::Cover: {
            const QSize target = isz.scaled(rect.size(), Qt::KeepAspectRatioByExpanding);
            QRect r(QPoint(0, 0), target);
            r.moveCenter(rect.center());
            p->drawImage(r, image);
            break;
        }
        case BgFit::Contain: {
            QRect r(QPoint(0, 0), isz.scaled(rect.size(), Qt::KeepAspectRatio));
            r.moveCenter(rect.center());
            p->drawImage(r, image);
            break;
        }
        case BgFit::Stretch:
            p->drawImage(rect, image);
            break;
        case BgFit::Tile:
            p->fillRect(rect, QBrush(image));
            break;
        case BgFit::Center: {
            QRect r(QPoint(0, 0), isz);
            r.moveCenter(rect.center());
            p->drawImage(r, image);
            break;
        }
        }
        p->setOpacity(1.0);
    }
    p->restore();
}

} // namespace esp
