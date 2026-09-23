#include "ThemePreview.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace esp {

namespace {

// 虚拟坐标：与真实窗口尺寸一致（960x560），绘制时按 rect 等比缩放
constexpr int kW = 960;
constexpr int kH = 560;
constexpr int kTitleH = 46;
constexpr int kSidebarW = 228;
constexpr int kBarH = 66;
constexpr int kRadius = 14;

QColor withAlpha(const QColor &c, int alpha)
{
    QColor out(c);
    out.setAlpha(alpha);
    return out;
}

// 画一条"文本占位"横条
void bar(QPainter *p, int x, int y, int w, int h, const QColor &c, int radius = 4)
{
    p->setPen(Qt::NoPen);
    p->setBrush(c);
    p->drawRoundedRect(QRectF(x, y, w, h), radius, radius);
}

QLinearGradient grad(int x1, int y1, int x2, int y2, const QColor &a, const QColor &b)
{
    QLinearGradient g(x1, y1, x2, y2);
    g.setColorAt(0, a);
    g.setColorAt(1, b);
    return g;
}

} // namespace

void paintMock(QPainter *p, const QRect &rect, const ThemeStyle &style,
               const MockSpec &spec, const QImage &bgImage)
{
    if (!p || rect.isEmpty())
        return;

    const bool uninst = spec.uninstaller;
    const QColor text(0x1F, 0x24, 0x30);
    const QColor muted(0x6B, 0x72, 0x80);

    // 等比缩放居中
    const qreal s = qMin(rect.width() / qreal(kW), rect.height() / qreal(kH));
    const qreal ox = rect.x() + (rect.width() - kW * s) / 2.0;
    const qreal oy = rect.y() + (rect.height() - kH * s) / 2.0;
    const bool drawText = spec.showText && s >= 0.55;

    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->translate(ox, oy);
    p->scale(s, s);

    // ---- 卡片底 + 背景图（与运行时 BackdropCard 相同的绘制顺序）----
    paintBackground(p, QRect(0, 0, kW, kH), style, bgImage, QColor(Qt::white), kRadius);

    // ---- 标题栏（透明，压在背景图上）----
    if (drawText) {
        p->setPen(text);
        QFont f(QStringLiteral("Segoe UI"), 9);
        f.setPixelSize(13);
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->drawText(QRect(46, 0, 500, kTitleH), Qt::AlignVCenter | Qt::AlignLeft,
                    spec.title.isEmpty() ? spec.appName : spec.title);
    }
    // 标题栏图标
    p->setPen(Qt::NoPen);
    p->setBrush(grad(20, 12, 20, 34, style.primary, style.secondary));
    p->drawRoundedRect(QRectF(18, 12, 22, 22), 6, 6);
    // 最小化 / 关闭按钮
    bar(p, kW - 84, 8, 36, 30, withAlpha(text, 18), 6);
    bar(p, kW - 44, 8, 36, 30, withAlpha(text, 18), 6);
    // 标题栏分隔线
    p->fillRect(QRect(0, kTitleH, kW, 1), withAlpha(text, 20));

    // ---- 侧栏 ----
    {
        const int sideA = style.sidebarAlpha();
        QLinearGradient g = grad(0, kTitleH + 1, 0, kH, style.sidebarStartColor(), style.sidebarEndColor());
        g.setColorAt(0, withAlpha(style.sidebarStartColor(), sideA));
        g.setColorAt(1, withAlpha(style.sidebarEndColor(), sideA));
        QPainterPath path;
        path.addRoundedRect(QRectF(0, kTitleH + 1, kSidebarW, kH - kTitleH - 1), kRadius, kRadius);
        // 只保留左下圆角：用矩形减去右上角区域
        QPainterPath square;
        square.addRect(QRectF(1, kTitleH + 1, kSidebarW - 1, kH - kTitleH - 1));
        p->setClipPath(path.united(square));
        p->setPen(Qt::NoPen);
        p->setBrush(g);
        p->drawRect(QRect(0, kTitleH + 1, kSidebarW, kH - kTitleH - 1));
        p->setClipping(false);
    }
    // 侧栏 logo
    p->setPen(Qt::NoPen);
    p->setBrush(withAlpha(Qt::white, 235));
    p->drawRoundedRect(QRectF(20, 73, 44, 44), 10, 10);
    p->setBrush(withAlpha(Qt::white, 90));
    p->drawRoundedRect(QRectF(30, 87, 24, 16), 3, 3);

    if (drawText) {
        QFont f(QStringLiteral("Segoe UI"), 9);
        f.setPixelSize(15);
        f.setWeight(QFont::Bold);
        p->setFont(f);
        p->setPen(Qt::white);
        p->drawText(QRect(20, 131, kSidebarW - 40, 22), Qt::AlignVCenter | Qt::AlignLeft,
                    spec.appName.isEmpty() ? QStringLiteral("My Application") : spec.appName);
        f.setPixelSize(12);
        f.setWeight(QFont::Normal);
        p->setFont(f);
        p->setPen(withAlpha(Qt::white, 210));
        if (!spec.subtitle.isEmpty())
            p->drawText(QRect(20, 155, kSidebarW - 40, 18), Qt::AlignVCenter | Qt::AlignLeft,
                        spec.subtitle);
    }

    // 步骤指示
    {
        int y = 195;
        const int n = spec.steps.isEmpty() ? (uninst ? 3 : 5) : spec.steps.size();
        for (int i = 0; i < n; ++i) {
            const bool active = (i == spec.activeStep);
            const bool hasText = drawText && i < spec.steps.size();
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(Qt::white, active ? 235 : 45));
            p->drawEllipse(QRectF(20, y + 1, 20, 20));
            // 有文字时只画文字，避免占位条与文字重叠
            if (!hasText)
                bar(p, 50, y + 7, active ? 118 : 96, 9, withAlpha(Qt::white, active ? 245 : 120), 4);
            if (hasText) {
                QFont f(QStringLiteral("Segoe UI"), 9);
                f.setPixelSize(12);
                f.setWeight(active ? QFont::DemiBold : QFont::Normal);
                p->setFont(f);
                p->setPen(withAlpha(Qt::white, active ? 255 : 165));
                p->drawText(QRect(52, y, 160, 22), Qt::AlignVCenter | Qt::AlignLeft, spec.steps.at(i));
            }
            y += 32;
        }
    }

    // ---- 内容区（标题 + 文本 + 进度条）----
    {
        const int cx = kSidebarW + 36;
        const int cw = kW - kSidebarW - 72;
        bar(p, cx, 77, int(cw * 0.52), 16, withAlpha(text, 235), 5);
        bar(p, cx, 112, int(cw * 0.78), 9, withAlpha(muted, 150), 4);
        bar(p, cx, 130, int(cw * 0.62), 9, withAlpha(muted, 150), 4);
        bar(p, cx, 148, int(cw * 0.70), 9, withAlpha(muted, 150), 4);

        // 进度条
        const int py = 196;
        bar(p, cx, py, cw, 10, QColor(0xE8, 0xED, 0xF6), 5);
        p->setPen(Qt::NoPen);
        p->setBrush(grad(cx, py, cx + int(cw * 0.45), py, style.primary, style.secondary));
        p->drawRoundedRect(QRectF(cx, py, cw * 0.45, 10), 5, 5);

        // 内容卡片（复选框区）
        p->setPen(QPen(QColor(0xE3, 0xE8, 0xF2), 1));
        p->setBrush(QColor(0xFB, 0xFC, 0xFE));
        p->drawRoundedRect(QRectF(cx, 228, cw, 120), 10, 10);
        int vy = 244;
        for (int i = 0; i < 3; ++i) {
            p->setPen(Qt::NoPen);
            p->setBrush(i == 0 ? style.primary : QColor(Qt::white));
            p->drawRoundedRect(QRectF(cx + 16, vy, 17, 17), 5, 5);
            if (i != 0) {
                p->setPen(QPen(QColor(0xC4, 0xCD, 0xDE), 1));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(QRectF(cx + 16, vy, 17, 17), 5, 5);
            }
            bar(p, cx + 44, vy + 4, int(cw * 0.45), 9, withAlpha(muted, 140), 4);
            vy += 32;
        }

        // 完成页的对勾圆点作为点缀
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0x18, 0xA0, 0x58));
        p->drawEllipse(QRectF(cx + cw - 52, 228 + 26, 36, 36));
    }

    // ---- 底部按钮栏 ----
    {
        const int sideA = style.barAlpha();
        const QColor barBase(0xFA, 0xFB, 0xFE, sideA);
        QPainterPath path;
        path.addRoundedRect(QRectF(kSidebarW, kH - kBarH, kW - kSidebarW, kBarH), kRadius, kRadius);
        QPainterPath square;
        square.addRect(QRectF(kSidebarW, kH - kBarH, kW - kSidebarW - 1, kBarH - 1));
        p->setClipPath(path.united(square));
        p->setPen(Qt::NoPen);
        p->setBrush(barBase);
        p->drawRect(QRect(kSidebarW, kH - kBarH, kW - kSidebarW, kBarH));
        p->setClipping(false);
        p->fillRect(QRect(kSidebarW, kH - kBarH, kW - kSidebarW, 1), withAlpha(text, 22));

        const int by = kH - kBarH + 14;
        auto button = [&](int x, int w, bool primary, const QString &label) {
            p->setPen(Qt::NoPen);
            if (primary) {
                const QColor c1 = uninst ? QColor(0xE5, 0x48, 0x4D) : style.primary;
                const QColor c2 = uninst ? QColor(0xC2, 0x34, 0x38) : style.secondary;
                p->setBrush(grad(x, by, x + w, by, c1, c2));
                p->drawRoundedRect(QRectF(x, by, w, 38), 8, 8);
            } else {
                p->setPen(QPen(QColor(0xD6, 0xDC, 0xE8), 1));
                p->setBrush(Qt::white);
                p->drawRoundedRect(QRectF(x, by, w, 38), 8, 8);
                p->setPen(Qt::NoPen);
            }
            if (drawText) {
                QFont f(QStringLiteral("Segoe UI"), 9);
                f.setPixelSize(13);
                f.setWeight(primary ? QFont::DemiBold : QFont::Normal);
                p->setFont(f);
                p->setPen(primary ? QColor(Qt::white) : text);
                p->drawText(QRectF(x, by, w, 38), Qt::AlignCenter, label);
            }
        };
        if (drawText) {
            button(kSidebarW + 24, 88, false, spec.cancelButton);
            button(kW - 24 - 108, 108, true, spec.primaryButton);
            button(kW - 24 - 108 - 10 - 100, 100, false, spec.backButton);
        } else {
            // 缩略图过小不画文字，但按钮形状仍要画出来（否则底栏看起来是空的）
            button(kSidebarW + 24, 88, false, QString());
            button(kW - 24 - 108, 108, true, QString());
            button(kW - 24 - 108 - 10 - 100, 100, false, QString());
        }
    }

    p->restore();
}

QPixmap renderMock(const QSize &size, const ThemeStyle &style, const MockSpec &spec)
{
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    const QImage bg = style.hasBackground()
                          ? decodeBackground(style.bgImage, qMax(640, size.width()))
                          : QImage();
    QPainter p(&pm);
    paintMock(&p, QRect(QPoint(0, 0), size), style, spec, bg);
    return pm;
}

} // namespace esp
