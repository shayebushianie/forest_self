#include "ui/GachaDigSiteWidget.h"

#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

namespace {

QPainterPath organicStone(const QPointF& center, qreal width, qreal height)
{
    QPainterPath stone;
    stone.moveTo(center.x() - width * 0.50, center.y() + height * 0.08);
    stone.cubicTo(center.x() - width * 0.42, center.y() - height * 0.42,
                  center.x() - width * 0.08, center.y() - height * 0.58,
                  center.x() + width * 0.24, center.y() - height * 0.38);
    stone.cubicTo(center.x() + width * 0.58, center.y() - height * 0.18,
                  center.x() + width * 0.48, center.y() + height * 0.38,
                  center.x() + width * 0.08, center.y() + height * 0.46);
    stone.cubicTo(center.x() - width * 0.20, center.y() + height * 0.50,
                  center.x() - width * 0.48, center.y() + height * 0.34,
                  center.x() - width * 0.50, center.y() + height * 0.08);
    return stone;
}

void drawGrassTuft(QPainter& painter, const QPointF& base, const QColor& color)
{
    painter.save();
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap));
    QPainterPath grass;
    grass.moveTo(base);
    grass.cubicTo(base.x() - 4, base.y() - 8, base.x() - 8, base.y() - 10,
                  base.x() - 10, base.y() - 15);
    grass.moveTo(base);
    grass.cubicTo(base.x(), base.y() - 7, base.x() + 1, base.y() - 12,
                  base.x() + 2, base.y() - 18);
    grass.moveTo(base);
    grass.cubicTo(base.x() + 5, base.y() - 7, base.x() + 10, base.y() - 9,
                  base.x() + 12, base.y() - 14);
    painter.drawPath(grass);
    painter.restore();
}

QRect opaqueBounds(const QPixmap& pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    int left = image.width();
    int top = image.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) <= 8) continue;
            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
        }
    }
    return right >= left && bottom >= top ? QRect(QPoint(left, top), QPoint(right, bottom)) : QRect();
}

QPixmap blendPlantBase(const QPixmap& source, const QRect& bounds)
{
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    const int blendTop = bounds.top() + qRound(bounds.height() * 0.74);
    const int blendHeight = qMax(1, bounds.bottom() - blendTop);
    const qreal centerX = bounds.center().x();
    const qreal feather = qMax(1.0, bounds.width() * 0.035);

    for (int y = blendTop; y <= bounds.bottom(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        const qreal progress = qBound(0.0, (y - blendTop) / static_cast<qreal>(blendHeight), 1.0);
        const qreal halfRootWidth = bounds.width() * (0.14 - progress * 0.11);
        const qreal verticalOpacity = qMax(0.0, 1.0 - progress * progress);
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha == 0) continue;
            const qreal outside = qAbs(x - centerX) - halfRootWidth;
            if (outside <= 0.0) continue;
            const qreal edgeOpacity = qBound(0.0, 1.0 - outside / feather, 1.0);
            const qreal opacity = edgeOpacity * verticalOpacity;
            line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]),
                            qRound(alpha * opacity));
        }
    }
    return QPixmap::fromImage(image);
}

} // namespace

GachaDigSiteWidget::GachaDigSiteWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("gachaDigSite");
    setMinimumHeight(300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GachaDigSiteWidget::setDiscoveredPlant(const QPixmap& pixmap, const QColor& accent)
{
    discoveredPlantBounds_ = opaqueBounds(pixmap);
    blendedPlant_ = discoveredPlantBounds_.isEmpty()
        ? QPixmap()
        : blendPlantBase(pixmap, discoveredPlantBounds_);
    if (!blendedPlant_.isNull()) {
        discoveredPlantBounds_ = opaqueBounds(blendedPlant_);
    }
    accent_ = accent.isValid() ? accent : QColor("#7BC7A7");
    if (accent_.lightness() < 95) accent_ = QColor("#BDD59D");
    hasResult_ = !discoveredPlantBounds_.isEmpty();
    update();
}

void GachaDigSiteWidget::clearDiscoveredPlant()
{
    blendedPlant_ = {};
    discoveredPlantBounds_ = {};
    hasResult_ = false;
    update();
}

void GachaDigSiteWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRectF panel = rect().adjusted(1, 1, -1, -1);
    QLinearGradient clearing(panel.topLeft(), panel.bottomRight());
    clearing.setColorAt(0.0, QColor("#EAF4CF"));
    clearing.setColorAt(0.56, QColor("#CFE7B9"));
    clearing.setColorAt(1.0, QColor("#AFCFA8"));
    painter.setBrush(clearing);
    painter.setPen(QPen(QColor("#78A77D"), 2));
    painter.drawRoundedRect(panel, 24, 24);

    const qreal scale = qMin(width() / 600.0, height() / 360.0);
    painter.save();
    painter.translate((width() - 600.0 * scale) / 2.0, (height() - 360.0 * scale) / 2.0);
    painter.scale(scale, scale);

    QPainterPath farShrubs;
    farShrubs.moveTo(34, 203);
    farShrubs.cubicTo(75, 154, 119, 177, 157, 142);
    farShrubs.cubicTo(205, 101, 247, 156, 293, 129);
    farShrubs.cubicTo(344, 97, 397, 158, 443, 137);
    farShrubs.cubicTo(499, 111, 550, 158, 573, 198);
    farShrubs.lineTo(573, 224);
    farShrubs.lineTo(34, 224);
    farShrubs.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(76, 134, 85, 70));
    painter.drawPath(farShrubs);

    painter.setBrush(QColor(42, 92, 65, 42));
    painter.drawEllipse(QRectF(79, 259, 442, 25));

    QPainterPath clearingEdge;
    clearingEdge.moveTo(66, 211);
    clearingEdge.cubicTo(103, 184, 151, 192, 195, 177);
    clearingEdge.cubicTo(242, 160, 276, 181, 309, 171);
    clearingEdge.cubicTo(361, 158, 401, 180, 444, 181);
    clearingEdge.cubicTo(493, 181, 527, 198, 539, 219);
    clearingEdge.cubicTo(513, 248, 460, 254, 411, 249);
    clearingEdge.cubicTo(356, 263, 307, 252, 260, 258);
    clearingEdge.cubicTo(202, 263, 143, 254, 101, 244);
    clearingEdge.cubicTo(79, 238, 66, 225, 66, 211);
    clearingEdge.closeSubpath();
    painter.setBrush(QColor("#79A85C"));
    painter.setPen(QPen(QColor("#557E4A"), 2));
    painter.drawPath(clearingEdge);

    QPainterPath bareSoil;
    bareSoil.moveTo(101, 211);
    bareSoil.cubicTo(135, 191, 180, 199, 218, 186);
    bareSoil.cubicTo(256, 175, 284, 190, 316, 183);
    bareSoil.cubicTo(363, 174, 398, 192, 438, 191);
    bareSoil.cubicTo(475, 191, 501, 203, 510, 220);
    bareSoil.cubicTo(486, 239, 447, 243, 406, 238);
    bareSoil.cubicTo(359, 249, 314, 239, 273, 246);
    bareSoil.cubicTo(222, 251, 174, 242, 139, 238);
    bareSoil.cubicTo(117, 235, 103, 224, 101, 211);
    bareSoil.closeSubpath();
    QLinearGradient soilGradient(0, 182, 0, 248);
    soilGradient.setColorAt(0.0, QColor("#D5A064"));
    soilGradient.setColorAt(1.0, QColor("#AD7044"));
    painter.setBrush(soilGradient);
    painter.setPen(QPen(QColor("#875438"), 1.5));
    painter.drawPath(bareSoil);

    if (hasResult_) {
        QRadialGradient revealGlow(QPointF(300, 132), 93);
        revealGlow.setColorAt(0.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 84));
        revealGlow.setColorAt(0.72, QColor(accent_.red(), accent_.green(), accent_.blue(), 26));
        revealGlow.setColorAt(1.0, QColor(accent_.red(), accent_.green(), accent_.blue(), 0));
        painter.setBrush(revealGlow);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QRectF(207, 39, 186, 186));
    }

    painter.setBrush(QColor(68, 48, 34, 55));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(244, 202, 112, 29));
    if (!hasResult_) {
        painter.setBrush(QColor("#694632"));
        painter.setPen(QPen(QColor("#82563A"), 2));
        painter.drawEllipse(QRectF(245, 181, 110, 55));
    }

    painter.setPen(QPen(QColor("#795133"), 9, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(112, 141), QPointF(171, 195));
    painter.setPen(QPen(QColor("#D8A45B"), 5, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(112, 141), QPointF(169, 193));
    QPainterPath blade;
    blade.moveTo(99, 129);
    blade.cubicTo(83, 132, 77, 148, 80, 162);
    blade.lineTo(115, 141);
    blade.closeSubpath();
    painter.setBrush(QColor("#8BA2A0"));
    painter.setPen(QPen(QColor("#5A7372"), 2));
    painter.drawPath(blade);

    QRectF plantRect;
    if (hasResult_) {
        if (!discoveredPlantBounds_.isEmpty()) {
            const qreal plantScale = qMin(184.0 / discoveredPlantBounds_.width(),
                                          184.0 / discoveredPlantBounds_.height());
            const QSizeF plantSize(discoveredPlantBounds_.width() * plantScale,
                                   discoveredPlantBounds_.height() * plantScale);
            plantRect = QRectF(300.0 - plantSize.width() / 2.0, 222.0 - plantSize.height(),
                               plantSize.width(), plantSize.height());
            painter.save();
            painter.setPen(QPen(QColor(118, 81, 59, 220), 3.2, Qt::SolidLine, Qt::RoundCap));
            QPainterPath roots;
            roots.moveTo(300, 212);
            roots.cubicTo(294, 216, 288, 220, 280, 222);
            roots.moveTo(300, 212);
            roots.cubicTo(306, 216, 312, 220, 320, 222);
            roots.moveTo(296, 216);
            roots.cubicTo(293, 219, 291, 221, 288, 223);
            painter.drawPath(roots);
            painter.restore();
            painter.drawPixmap(plantRect, blendedPlant_, QRectF(discoveredPlantBounds_));
        }
    }

    painter.setPen(Qt::NoPen);
    const QPointF specks[] = {{263, 217}, {286, 231}, {315, 222}, {340, 232}};
    for (int i = 0; i < 4; ++i) {
        painter.setBrush(i % 2 == 0 ? QColor("#E4B477") : QColor("#815039"));
        painter.drawEllipse(specks[i], i % 2 == 0 ? 3.0 : 2.3, 1.7);
    }

    const struct { QPointF center; qreal width; qreal height; QColor color; } stones[] = {
        {{135, 237}, 31, 18, QColor("#8E7B64")}, {{455, 239}, 27, 17, QColor("#A39274")},
        {{501, 216}, 20, 13, QColor("#766B5B")}};
    for (const auto& stone : stones) {
        painter.setBrush(stone.color);
        painter.setPen(QPen(stone.color.darker(125), 1.5));
        painter.drawPath(organicStone(stone.center, stone.width, stone.height));
    }

    drawGrassTuft(painter, QPointF(91, 219), QColor("#568C51"));
    drawGrassTuft(painter, QPointF(153, 197), QColor("#689855"));
    drawGrassTuft(painter, QPointF(459, 196), QColor("#6C9C55"));
    drawGrassTuft(painter, QPointF(521, 222), QColor("#4F884F"));

    if (hasResult_) {
        painter.setBrush(accent_.lighter(138));
        painter.setPen(Qt::NoPen);
        const QPointF sparks[] = {{222, 92}, {389, 108}, {406, 159}};
        for (int i = 0; i < 3; ++i) {
            QPainterPath spark;
            const qreal size = i == 1 ? 3.5 : 4.5;
            spark.moveTo(sparks[i].x(), sparks[i].y() - size);
            spark.lineTo(sparks[i].x() + size * 0.5, sparks[i].y());
            spark.lineTo(sparks[i].x(), sparks[i].y() + size);
            spark.lineTo(sparks[i].x() - size * 0.5, sparks[i].y());
            spark.closeSubpath();
            painter.drawPath(spark);
        }
    } else {
        painter.setPen(QPen(QColor("#73D9B2"), 2.5, Qt::SolidLine, Qt::RoundCap));
        QPainterPath fissure;
        fissure.moveTo(284, 211);
        fissure.lineTo(296, 219);
        fissure.lineTo(289, 229);
        fissure.moveTo(316, 210);
        fissure.lineTo(307, 219);
        fissure.lineTo(315, 228);
        painter.drawPath(fissure);

        QPainterPath shard;
        shard.moveTo(300, 176);
        shard.lineTo(313, 197);
        shard.lineTo(304, 211);
        shard.lineTo(288, 196);
        shard.closeSubpath();
        QLinearGradient shardGradient(290, 176, 313, 211);
        shardGradient.setColorAt(0.0, QColor("#BDF1C5"));
        shardGradient.setColorAt(1.0, QColor("#5DBB9F"));
        painter.setBrush(shardGradient);
        painter.setPen(QPen(QColor("#397C68"), 2));
        painter.drawPath(shard);
    }

    painter.restore();
}
