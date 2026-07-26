#include "ui/PlantTimerWidget.h"
#include "config/PlantCatalog.h"
#include "ui/PaintedActionButton.h"
#include "ui/PlantImageUtils.h"
#include "ui/AppStyle.h"
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QSizePolicy>
#include <QFontMetrics>
#include <QtMath>
#include <cmath>

PlantTimerWidget::PlantTimerWidget(QWidget* parent) : QAbstractSlider(parent)
{
    setRange(10, 120);
    setSingleStep(1);
    setPageStep(5);
    setValue(25);
    connect(this, &QAbstractSlider::valueChanged, this, [this](int minutes) {
        if (selectedMinutes_ == static_cast<uint32_t>(minutes)) return;
        selectedMinutes_ = static_cast<uint32_t>(minutes);
        if (!isRunning_ && !isStopwatch_) displaySeconds_ = selectedMinutes_ * 60;
        knobAngle_ = minutesToAngle(selectedMinutes_);
        emit sig_timeSelected(selectedMinutes_);
        update();
    });
    setMinimumSize(280, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("Focus duration timer"));
    setAccessibleDescription(QStringLiteral("Use arrow keys to adjust one minute and Page Up or Page Down to adjust five minutes."));
    tagAction_ = new PaintedActionButton(this);
    tagAction_->setObjectName("timerTagAction");
    tagAction_->setFocusPolicy(Qt::StrongFocus);
    tagAction_->setAccessibleName(QStringLiteral("Choose focus tag"));
    tagAction_->setAccessibleDescription(QStringLiteral("Opens the focus tag selector."));
    tagAction_->setToolTip(QStringLiteral("选择项目标签"));
    tagAction_->setStyleSheet(QStringLiteral(
        "QToolButton { background:transparent; border:2px solid transparent; border-radius:15px; color:transparent; }"
        "QToolButton:hover { background:rgba(255,255,255,16); }"
        "QToolButton:pressed { background:rgba(36,69,56,28); }"));
    plantAction_ = new PaintedActionButton(this);
    plantAction_->setObjectName("timerPlantAction");
    plantAction_->setFocusPolicy(Qt::StrongFocus);
    plantAction_->setAccessibleName(QStringLiteral("Choose focus plant"));
    plantAction_->setAccessibleDescription(QStringLiteral("Opens the focus plant selector."));
    plantAction_->setToolTip(QStringLiteral("选择专注植物"));
    plantAction_->setStyleSheet(QStringLiteral(
        "QToolButton { background:transparent; border:2px solid transparent; border-radius:18px; color:transparent; }"
        "QToolButton:hover { background:rgba(255,255,255,16); }"
        "QToolButton:pressed { background:rgba(36,69,56,28); }"));
    connect(tagAction_, &PaintedActionButton::clicked, this, &PlantTimerWidget::sig_tagClicked);
    connect(plantAction_, &PaintedActionButton::clicked, this, &PlantTimerWidget::sig_plantClicked);
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
    setValue(static_cast<int>(selectedMinutes_));
    if (!isRunning_ && !isStopwatch_) displaySeconds_ = selectedMinutes_ * 60;
    knobAngle_ = minutesToAngle(selectedMinutes_);
    update();
}

void PlantTimerWidget::resizeEvent(QResizeEvent*)
{
    int reservedBottom = 200;
    int reservedTop = 36;
    int side = qMin(width() - 90, height() - reservedTop - reservedBottom);
    side = qBound(130, side, 380);
    ringRadius_ = side / 2.0;
    ringCenterX_ = width() / 2.0;
    double maxCenterY = height() - reservedBottom - ringRadius_;
    double preferredCenterY = qMin(height() * 0.455, 287.0);
    ringCenterY_ = qBound(ringRadius_ + 28, preferredCenterY, maxCenterY);
    ringRect_ = QRectF(ringCenterX_ - ringRadius_, ringCenterY_ - ringRadius_,
                       ringRadius_ * 2, ringRadius_ * 2);
    updateSemanticActionGeometry();
}

void PlantTimerWidget::keyPressEvent(QKeyEvent* event)
{
    int adjustment = 0;
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        adjustment = -1;
        break;
    case Qt::Key_Right:
    case Qt::Key_Up:
        adjustment = 1;
        break;
    case Qt::Key_PageDown:
        adjustment = -5;
        break;
    case Qt::Key_PageUp:
        adjustment = 5;
        break;
    default:
        QAbstractSlider::keyPressEvent(event);
        return;
    }
    if (!selectorVisible()) return;
    const uint32_t previousMinutes = selectedMinutes_;
    setSelectedMinutes(static_cast<uint32_t>(qBound(10, static_cast<int>(selectedMinutes_) + adjustment, 120)));
    if (selectedMinutes_ != previousMinutes) emit sig_timeSelected(selectedMinutes_);
    event->accept();
}

void PlantTimerWidget::updateSemanticActionGeometry()
{
    if (tagAction_) tagAction_->setGeometry(tagRect_.toAlignedRect());
    if (plantAction_) {
        plantAction_->setGeometry(plantRect_.toAlignedRect());
        plantAction_->setVisible(selectorVisible() && !plantRect_.isEmpty());
    }
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
    if (!selectorVisible()) return;
    if (isOnKnob(pos)) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
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
            setValue(static_cast<int>(selectedMinutes_));
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
    QRadialGradient ambient(QPointF(width() * 0.50, height() * 0.36), width() * 0.38);
    ambient.setColorAt(0.0, QColor(255, 255, 255, 26));
    ambient.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setBrush(ambient);
    painter.drawEllipse(QRectF(width() * 0.12, height() * 0.04, width() * 0.76, height() * 0.68));

    const bool showSelector = selectorVisible();
    QRectF glowRect = ringRect_.adjusted(-20, -20, 20, 20);
    QRadialGradient glow(glowRect.center(), glowRect.width() * 0.55);
    glow.setColorAt(0.0, QColor(247, 255, 233, 34));
    glow.setColorAt(1.0, QColor(247, 255, 233, 0));
    painter.setBrush(glow);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(glowRect);

    painter.setBrush(QColor(35, 103, 77, 30));
    painter.drawEllipse(ringRect_.translated(0, 8));

    painter.setBrush(QColor("#F2EFAF"));
    painter.setPen(QPen(QColor("#E4E797"), 2));
    painter.drawEllipse(ringRect_);

    QRectF innerRect = ringRect_.adjusted(14, 14, -14, -14);
    painter.setBrush(QColor("#F8F4C8"));
    painter.setPen(QPen(QColor(255, 255, 255, 80), 1));
    painter.drawEllipse(innerRect);

    if (showSelector) {
        QPen trackPen;
        trackPen.setColor(QColor("#D8DE86"));
        trackPen.setWidth(8);
        trackPen.setCapStyle(Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawEllipse(ringRect_.adjusted(6, 6, -6, -6));

        if (knobAngle_ > 1) {
            QPen progressPen;
            progressPen.setColor(QColor("#A4D33A"));
            progressPen.setWidth(9);
            progressPen.setCapStyle(Qt::RoundCap);
            painter.setPen(progressPen);
            painter.drawArc(ringRect_.adjusted(6, 6, -6, -6), 90 * 16,
                            -static_cast<int>(knobAngle_ * 16));
        }

        QPointF kc = knobCenter(knobAngle_);
        painter.setBrush(QColor("#A9D23B"));
        QPen knobBorder;
        knobBorder.setColor(QColor("#BFE15C"));
        knobBorder.setWidth(2);
        painter.setPen(knobBorder);
        painter.drawEllipse(kc, 11, 11);
        painter.setBrush(QColor(255, 255, 255, 105));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(kc.x() - 3, kc.y() - 4), 3, 3);
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
    if (showSelector && AppStyle::keyboardFocusVisible(plantAction_) && !plantRect_.isEmpty()) {
        painter.setPen(QPen(QColor("#6E6A2C"), 1));
        painter.setBrush(QColor("#F4D970"));
        painter.drawEllipse(QPointF(plantRect_.center().x(), plantRect_.top() + 8), 4, 4);
    }

    if (showSelector) {
        const QRectF guideRect(innerRect.center().x() - 94, qMax<qreal>(4, ringRect_.top() - 40),
                               188, 24);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(250, 248, 218, 218));
        painter.drawRoundedRect(guideRect, 12, 12);
        painter.setFont(QFont("Microsoft YaHei", 9, QFont::DemiBold));
        painter.setPen(QColor("#557540"));
        painter.drawText(guideRect, Qt::AlignCenter, QStringLiteral("拖动光环调整时长"));
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
    painter.setBrush(QColor(255, 255, 255, 42));
    painter.setPen(QPen(QColor(255, 255, 255, 72), 1));
    painter.drawRoundedRect(tagRect_, 15, 15);
    painter.setBrush(tagColor_);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(tagRect_.left() + 20, tagRect_.center().y()), 6, 6);
    painter.setFont(tagFont);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(tagRect_.adjusted(24, 0, -8, 0), Qt::AlignCenter, tagText_);
    if (AppStyle::keyboardFocusVisible(tagAction_)) {
        painter.setPen(QPen(QColor("#F4D970"), 3, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(tagRect_.center().x() - 16, tagRect_.bottom() - 6),
                         QPointF(tagRect_.center().x() + 16, tagRect_.bottom() - 6));
    }
    updateSemanticActionGeometry();

    int timePointSize = qBound(40, static_cast<int>(height() * 0.118), 62);
    QFont timeFont("Segoe UI Light", timePointSize, QFont::Light);
    painter.setFont(timeFont);
    painter.setPen(QColor("#F7FFF7"));
    const QFontMetricsF timeMetrics(timeFont);
    const qreal timeHeight = timeMetrics.height() + 8;
    const qreal timeTop = qMin(tagRect_.bottom() + 24, height() - timeHeight - 12);
    const QRectF timeRect(0, timeTop, width(), timeHeight);
    painter.setFont(timeFont);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(timeRect, Qt::AlignCenter, timeStr);

    QString topText = !oath_.isEmpty() ? oath_ : quote_;
    if (!topText.isEmpty()) {
        QFont oathFont("Microsoft YaHei", 12, QFont::Bold);
        painter.setFont(oathFont);
        painter.setPen(QColor("#F2F4C6"));
        QRectF oathRect(0, ringCenterY_ - ringRadius_ - 34, width(), 25);
        painter.setPen(QColor("#F2F4C6"));
        painter.drawText(oathRect, Qt::AlignCenter, topText);
    }
}
