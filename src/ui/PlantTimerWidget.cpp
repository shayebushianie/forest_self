#include "ui/PlantTimerWidget.h"
#include "config/PlantCatalog.h"
#include "ui/PlantImageUtils.h"
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QFontMetrics>
#include <QtMath>
#include <cmath>

PlantTimerWidget::PlantTimerWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(280, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    selectedMinutes_ = 25;
    knobAngle_ = minutesToAngle(selectedMinutes_);
}

QSize PlantTimerWidget::sizeHint() const
{
    return QSize(640, 520);
}

QSize PlantTimerWidget::minimumSizeHint() const
{
    return QSize(280, 300);
}

void PlantTimerWidget::setDisplaySeconds(uint32_t seconds, bool isStopwatch)
{
    const bool wasRunning = isRunning_;
    displaySeconds_ = seconds;
    isStopwatch_ = isStopwatch;
    isRunning_ = (seconds > 0);

    if (!isStopwatch_ && !isRunning_) {
        totalSeconds_ = selectedMinutes_ * 60;
        knobAngle_ = minutesToAngle(selectedMinutes_);
    } else if (!isStopwatch_ && !wasRunning) {
        totalSeconds_ = seconds;
    } else if (!isStopwatch_ && (totalSeconds_ == 0 || seconds > totalSeconds_)) {
        totalSeconds_ = seconds;
    }
    update();
}

void PlantTimerWidget::setQuote(const QString& quote) { quote_ = quote; update(); }
void PlantTimerWidget::setOath(const QString& oath) { oath_ = oath; update(); }
void PlantTimerWidget::setPlantType(uint32_t type) { plantType_ = type; update(); }
void PlantTimerWidget::setTagInfo(const QString& text, const QColor& color)
{
    tagText_ = text.trimmed().isEmpty() ? QStringLiteral("无标签") : text.trimmed();
    tagColor_ = color.isValid() ? color : QColor("#B8B6EA");
    update();
}

void PlantTimerWidget::setSelectedMinutes(uint32_t minutes)
{
    selectedMinutes_ = qBound<uint32_t>(10, minutes, 120);
    if (!isRunning_ && !isStopwatch_) displaySeconds_ = selectedMinutes_ * 60;
    knobAngle_ = minutesToAngle(selectedMinutes_);
    update();
}

void PlantTimerWidget::resizeEvent(QResizeEvent*)
{
    int reservedBottom = 140;
    int reservedTop = 36;
    int side = qMin(width() - 90, height() - reservedTop - reservedBottom);
    side = qBound(130, side, 430);
    ringRadius_ = side / 2.0;
    ringCenterX_ = width() / 2.0;
    double maxCenterY = height() - reservedBottom - ringRadius_;
    double preferredCenterY = qMin(height() * 0.455, 287.0);
    ringCenterY_ = qBound(ringRadius_ + 28, preferredCenterY, maxCenterY);
    ringRect_ = QRectF(ringCenterX_ - ringRadius_, ringCenterY_ - ringRadius_,
                       ringRadius_ * 2, ringRadius_ * 2);
}

double PlantTimerWidget::angleFromPoint(const QPointF& p) const
{
    double dx = p.x() - ringCenterX_;
    double dy = -(p.y() - ringCenterY_);
    double angle = std::atan2(dx, dy) * 180.0 / 3.14159265;
    if (angle < 0) angle += 360;
    return angle;
}

double PlantTimerWidget::angleToMinutes(double angleDeg) const
{
    double mins = 10.0 + (angleDeg / 360.0) * 110.0;
    return qBound(10.0, mins, 120.0);
}

double PlantTimerWidget::minutesToAngle(uint32_t minutes) const
{
    const double clampedMinutes = qBound(10.0, static_cast<double>(minutes), 120.0);
    return (clampedMinutes - 10.0) * 360.0 / 110.0;
}

QPointF PlantTimerWidget::knobCenter(double angleDeg) const
{
    double rad = angleDeg * 3.14159265 / 180.0;
    double x = ringCenterX_ + ringRadius_ * std::sin(rad);
    double y = ringCenterY_ - ringRadius_ * std::cos(rad);
    return QPointF(x, y);
}

bool PlantTimerWidget::isOnKnob(const QPointF& p) const
{
    QPointF kc = knobCenter(knobAngle_);
    double dx = p.x() - kc.x();
    double dy = p.y() - kc.y();
    return (dx * dx + dy * dy) <= 400;
}

bool PlantTimerWidget::selectorVisible() const
{
    return !isRunning_ && !isStopwatch_;
}

void PlantTimerWidget::mousePressEvent(QMouseEvent* event)
{
    QPointF pos = event->position();
    if (tagRect_.contains(pos)) {
        emit sig_tagClicked();
        return;
    }
    if (!selectorVisible()) return;
    if (isOnKnob(pos)) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (plantRect_.contains(pos)) {
        emit sig_plantClicked();
    }
}

void PlantTimerWidget::mouseMoveEvent(QMouseEvent* event)
{
    QPointF pos = event->position();

    if (dragging_) {
        double angle = angleFromPoint(pos);
        double mins = angleToMinutes(angle);
        uint32_t newMins = static_cast<uint32_t>(qRound(mins));
        if (newMins != selectedMinutes_) {
            selectedMinutes_ = newMins;
            displaySeconds_ = selectedMinutes_ * 60;
            knobAngle_ = minutesToAngle(selectedMinutes_);
            emit sig_timeSelected(selectedMinutes_);
            update();
        }
        return;
    }

    if (tagRect_.contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }
    if (selectorVisible() && plantRect_.contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        return;
    }

    if (selectorVisible()) {
        setCursor(isOnKnob(pos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
}

void PlantTimerWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
}

// ---------------------------------------------------------------------------
// Pixmap cache for plant images
// ---------------------------------------------------------------------------

static QRectF pixmapContentRect(const QPixmap& pixmap, const QRectF& targetRect)
{
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return targetRect;

    int minX = image.width();
    int minY = image.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 18) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }

    if (maxX < minX || maxY < minY) return QRectF();

    const qreal sx = targetRect.width() / image.width();
    const qreal sy = targetRect.height() / image.height();
    QRectF contentRect(targetRect.left() + minX * sx,
                       targetRect.top() + minY * sy,
                       (maxX - minX + 1) * sx,
                       (maxY - minY + 1) * sy);
    return contentRect.adjusted(-8, -8, 8, 8).intersected(targetRect);
}

const QPixmap& PlantTimerWidget::plantPixmap(uint32_t plantType, uint32_t stage, bool withered)
{
    const QString path = PlantCatalog::stageImagePath(plantType, stage, withered);
    const QString key = path;

    auto it = plantCache_.find(key);
    if (it != plantCache_.end()) {
        return it.value();
    }

    QPixmap pm = PlantImageUtils::loadPlantIcon(path);
    if (pm.isNull()) {
        static const QPixmap nullPm;
        return nullPm;
    }
    plantCache_[key] = pm;
    return plantCache_[key];
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void PlantTimerWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(Qt::NoPen);
    QRadialGradient ambient(QPointF(width() * 0.50, height() * 0.36), width() * 0.44);
    ambient.setColorAt(0.0, QColor(247, 244, 198, 34));
    ambient.setColorAt(0.68, QColor(205, 238, 193, 16));
    ambient.setColorAt(1.0, QColor(205, 238, 193, 0));
    painter.setBrush(ambient);
    painter.drawEllipse(QRectF(width() * 0.06, height() * 0.02, width() * 0.88, height() * 0.76));

    painter.setPen(QPen(QColor(255, 255, 255, 36), 1, Qt::SolidLine, Qt::RoundCap));
    for (int x = 30; x < width(); x += 52) {
        painter.drawLine(x, 22, x + 18, 22);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(244, 250, 222, 58));
    for (int i = 0; i < 14; ++i) {
        const qreal x = 34 + (i * 83) % qMax(1, width() - 68);
        const qreal y = 54 + (i * 61) % qMax(1, qMax(86, height() - 150));
        painter.drawEllipse(QRectF(x, y, 3.5, 3.5));
    }
    const QVector<QPair<QPointF, QSizeF>> leafMotifs = {
        {QPointF(width() * 0.17, height() * 0.36), QSizeF(42, 18)},
        {QPointF(width() * 0.80, height() * 0.30), QSizeF(34, 15)},
        {QPointF(width() * 0.22, height() * 0.68), QSizeF(30, 13)},
        {QPointF(width() * 0.78, height() * 0.68), QSizeF(44, 18)},
    };
    painter.setBrush(QColor(220, 247, 200, 24));
    for (int i = 0; i < leafMotifs.size(); ++i) {
        painter.save();
        painter.translate(leafMotifs[i].first);
        painter.rotate(i % 2 == 0 ? -28 : 28);
        const QSizeF size = leafMotifs[i].second;
        painter.drawEllipse(QRectF(-size.width() / 2, -size.height() / 2,
                                  size.width(), size.height()));
        painter.restore();
    }

    const bool showSelector = selectorVisible();
    QRectF glowRect = ringRect_.adjusted(-32, -32, 32, 32);
    QRadialGradient glow(glowRect.center(), glowRect.width() * 0.55);
    glow.setColorAt(0.0, QColor(255, 255, 255, 82));
    glow.setColorAt(0.62, QColor(242, 244, 198, 78));
    glow.setColorAt(1.0, QColor(242, 244, 198, 0));
    painter.setBrush(glow);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(glowRect);

    painter.setBrush(QColor(35, 103, 77, 44));
    painter.drawEllipse(ringRect_.translated(0, 13).adjusted(-5, -2, 5, 8));

    QLinearGradient rimGradient(ringRect_.topLeft(), ringRect_.bottomRight());
    rimGradient.setColorAt(0.0, QColor("#FFF6B8"));
    rimGradient.setColorAt(0.52, QColor("#E8EC84"));
    rimGradient.setColorAt(1.0, QColor("#B9D84A"));
    painter.setBrush(rimGradient);
    painter.setPen(QPen(QColor("#DCE56A"), 3));
    painter.drawEllipse(ringRect_);

    QRectF innerRect = showSelector ? ringRect_.adjusted(18, 18, -18, -18)
                                    : ringRect_.adjusted(22, 22, -22, -22);
    QRadialGradient sanctuary(innerRect.center() - QPointF(innerRect.width() * 0.12,
                                                            innerRect.height() * 0.16),
                            innerRect.width() * 0.70);
    sanctuary.setColorAt(0.0, QColor("#FFFCE4"));
    sanctuary.setColorAt(0.64, QColor("#F8F1B2"));
    sanctuary.setColorAt(1.0, QColor("#EDEB91"));
    painter.setBrush(sanctuary);
    painter.setPen(QPen(QColor(255, 255, 255, 120), 2));
    painter.drawEllipse(innerRect);

    if (showSelector) {
        QPen trackPen;
        trackPen.setColor(QColor(216, 203, 73, 166));
        trackPen.setWidth(10);
        trackPen.setCapStyle(Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawEllipse(ringRect_.adjusted(7, 7, -7, -7));

        if (knobAngle_ > 1) {
            QPen progressPen;
            progressPen.setColor(QColor("#A4D33A"));
            progressPen.setWidth(11);
            progressPen.setCapStyle(Qt::RoundCap);
            painter.setPen(progressPen);
            painter.drawArc(ringRect_.adjusted(7, 7, -7, -7), 90 * 16,
                            -static_cast<int>(knobAngle_ * 16));
        }

        QPointF kc = knobCenter(knobAngle_);
        painter.setBrush(QColor("#A9D23B"));
        QPen knobBorder;
        knobBorder.setColor(QColor("#BFE15C"));
        knobBorder.setWidth(2);
        painter.setPen(knobBorder);
        painter.drawEllipse(kc, 14, 14);
        painter.setBrush(QColor(255, 255, 255, 85));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(kc.x() - 4, kc.y() - 5), 4, 4);
    }

    painter.save();
    QPainterPath sanctuaryClip;
    sanctuaryClip.addEllipse(innerRect);
    painter.setClipPath(sanctuaryClip);
    painter.setPen(QPen(QColor("#E8D98F"), 1, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 12; ++i) {
        const qreal x = innerRect.left() + innerRect.width() * (0.16 + (i % 4) * 0.22);
        const qreal y = innerRect.top() + innerRect.height() * (0.18 + (i / 4) * 0.22);
        painter.drawLine(QPointF(x - 4, y + 2), QPointF(x + 4, y - 2));
    }
    painter.restore();

    if (showSelector) {
        const QRectF guideRect(innerRect.center().x() - 94, innerRect.top() + innerRect.height() * 0.13,
                               188, 24);
        painter.setBrush(QColor(255, 255, 255, 70));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(guideRect, 12, 12);
        painter.setFont(QFont("Microsoft YaHei", 9, QFont::DemiBold));
        painter.setPen(QColor("#5A813D"));
        painter.drawText(guideRect, Qt::AlignCenter, QStringLiteral("拖动光环调整时长"));
    }

    // --- growth stage from actual progress (or selected minutes if idle) ------
    double ratio;
    if (isRunning_) {
        if (isStopwatch_) {
            ratio = static_cast<double>(displaySeconds_) / 7200.0;
        } else {
            ratio = static_cast<double>(totalSeconds_ - displaySeconds_) / totalSeconds_;
        }
        ratio = qBound(0.0, ratio, 1.0);
    } else {
        ratio = (selectedMinutes_ - 10) / 110.0;           // 0.0 → 1.0
    }
    uint32_t stage;
    if (ratio < 0.12)      stage = 0;   // 10–23 min  → Seed
    else if (ratio < 0.40) stage = 1;   // 23–54 min  → Sprout
    else if (ratio < 0.70) stage = 2;   // 54–87 min  → Sapling
    else                    stage = 3;   // 87–120 min → Mature

    // --- plant image (scaled to fill the ring interior, clipped to circle) ----
    {
        const QPixmap& pm = plantPixmap(plantType_, stage, false);
        if (!pm.isNull()) {
            painter.save();
            QPainterPath clipPath;
            clipPath.addEllipse(innerRect);
            painter.setClipPath(clipPath);

            QSizeF targetSize = pm.size().scaled(innerRect.width(), innerRect.height(),
                                                  Qt::KeepAspectRatio);
            QRectF targetRect(
                innerRect.center().x() - targetSize.width() / 2,
                innerRect.center().y() - targetSize.height() / 2,
                targetSize.width(),
                targetSize.height());

            plantRect_ = pixmapContentRect(pm, targetRect);
            painter.drawPixmap(targetRect.toRect(), pm);
            painter.restore();
        } else {
            plantRect_ = QRectF();
        }
    }

    uint32_t secs = displaySeconds_;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    QString timeStr = (isRunning_ || isStopwatch_)
        ? QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QStringLiteral("%1:00").arg(selectedMinutes_, 2, 10, QChar('0'));

    qreal tagTop = qMin(ringRect_.bottom() + 28, height() - 146.0);
    QFont tagFont("Microsoft YaHei", 12, QFont::Bold);
    QFontMetrics tagMetrics(tagFont);
    const qreal tagWidth = qBound<qreal>(124, tagMetrics.horizontalAdvance(tagText_) + 62, 200);
    tagRect_ = QRectF(ringCenterX_ - tagWidth / 2.0, tagTop, tagWidth, 38);
    painter.setBrush(QColor(255, 255, 255, 50));
    painter.setPen(QPen(QColor(255, 255, 255, 58), 1));
    painter.drawRoundedRect(tagRect_, 15, 15);
    painter.setBrush(tagColor_);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(tagRect_.left() + 20, tagRect_.center().y()), 6, 6);
    painter.setFont(tagFont);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(tagRect_.adjusted(24, 0, -8, 0), Qt::AlignCenter, tagText_);

    int timePointSize = qBound(40, static_cast<int>(height() * 0.118), 62);
    QFont timeFont("Segoe UI Light", timePointSize, QFont::Light);
    painter.setFont(timeFont);
    painter.setPen(QColor("#F7FFF7"));
    const QFontMetricsF timeMetrics(timeFont);
    const qreal timeHeight = timeMetrics.height() + 8;
    const qreal timeTop = tagRect_.bottom() + 16;
    const qreal timeBottom = height() - 12;
    const qreal centeredTimeTop = timeTop + qMax<qreal>(0, (timeBottom - timeTop - timeHeight) / 2.0);
    const qreal preferredTimeOffset = qMax<qreal>(0, (height() - 630) / 2.0);
    const qreal visibleTimeOffset = qMin(preferredTimeOffset,
        qMax<qreal>(0, timeBottom - timeHeight - centeredTimeTop));
    QRectF timeRect(0, centeredTimeTop + visibleTimeOffset, width(), timeHeight);
    const qreal timeCardWidth = qBound<qreal>(300, timeMetrics.horizontalAdvance(timeStr) + 130, 420);
    const QRectF timeCard(ringCenterX_ - timeCardWidth / 2.0, timeRect.top() - 30,
                          timeCardWidth, timeRect.height() + 60);
    painter.setBrush(QColor(255, 255, 255, 32));
    painter.setPen(QPen(QColor(255, 255, 255, 64), 1));
    painter.drawRoundedRect(timeCard, 22, 22);
    painter.setFont(QFont("Microsoft YaHei", 9, QFont::DemiBold));
    painter.setPen(QColor(247, 255, 247, 175));
    painter.drawText(QRectF(timeCard.left(), timeCard.top() + 8, timeCard.width(), 18),
                     Qt::AlignCenter, isStopwatch_ ? QStringLiteral("正计时")
                                                    : QStringLiteral("本次专注时长"));
    painter.setFont(timeFont);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(timeRect, Qt::AlignCenter, timeStr);

    QString topText = !oath_.isEmpty() ? oath_ : quote_;
    if (!topText.isEmpty()) {
        QFont oathFont("Microsoft YaHei", 12, QFont::Bold);
        painter.setFont(oathFont);
        painter.setPen(QColor("#F2F4C6"));
        QRectF oathRect(0, ringCenterY_ - ringRadius_ - 34, width(), 25);
        painter.setBrush(QColor(43, 108, 84, 45));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(oathRect.adjusted(width() * 0.30, -2, -width() * 0.30, 2), 12, 12);
        painter.setPen(QColor("#F2F4C6"));
        painter.drawText(oathRect, Qt::AlignCenter, topText);
    }
}
