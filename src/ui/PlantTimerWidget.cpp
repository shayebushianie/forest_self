#include "ui/PlantTimerWidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QtMath>
#include <cmath>

PlantTimerWidget::PlantTimerWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(300, 380);
    setMouseTracking(true);
    selectedMinutes_ = 25;
    knobAngle_ = (selectedMinutes_ - 10) * 360.0 / 110.0;
}

void PlantTimerWidget::setDisplaySeconds(uint32_t seconds, bool isStopwatch)
{
    displaySeconds_ = seconds;
    isStopwatch_ = isStopwatch;
    if (!isStopwatch_ && totalSeconds_ > 0) {
        double ratio = static_cast<double>(seconds) / totalSeconds_;
        knobAngle_ = ratio * 360.0;
    }
    isRunning_ = (seconds > 0);
    update();
}

void PlantTimerWidget::setQuote(const QString&) {}
void PlantTimerWidget::setOath(const QString& oath) { oath_ = oath; update(); }
void PlantTimerWidget::setPlantType(uint32_t type) { plantType_ = type; update(); }

void PlantTimerWidget::resizeEvent(QResizeEvent*)
{
    int side = qMin(width(), height()) - 40;
    ringRadius_ = side / 2.0;
    ringCenterX_ = width() / 2.0;
    ringCenterY_ = height() / 2.0;
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

void PlantTimerWidget::mousePressEvent(QMouseEvent* event)
{
    if (isRunning_) return;
    QPointF pos = event->position();
    if (isOnKnob(pos)) {
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
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
            knobAngle_ = angle;
            emit sig_timeSelected(selectedMinutes_);
            update();
        }
    } else if (!isRunning_) {
        setCursor(isOnKnob(pos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
}

void PlantTimerWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
}

void PlantTimerWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Background ring (darker track)
    QPen trackPen;
    trackPen.setColor(QColor(46, 63, 52, 100));
    trackPen.setWidth(10);
    trackPen.setCapStyle(Qt::RoundCap);
    painter.setPen(trackPen);
    painter.drawEllipse(ringRect_);

    // Progress arc (green from 0 to current angle)
    if (knobAngle_ > 1) {
        QPen progressPen;
        progressPen.setColor(QColor("#4E9F3D"));
        progressPen.setWidth(10);
        progressPen.setCapStyle(Qt::RoundCap);
        painter.setPen(progressPen);
        painter.drawArc(ringRect_, 90 * 16, -static_cast<int>(knobAngle_ * 16));
    }

    // Knob circle on the ring
    QPointF kc = knobCenter(knobAngle_);
    QColor knobColor = isRunning_ ? QColor("#D8B257") : QColor("#4E9F3D");
    painter.setBrush(knobColor);
    QPen knobBorder;
    knobBorder.setColor(QColor("#1e2922"));
    knobBorder.setWidth(3);
    painter.setPen(knobBorder);
    painter.drawEllipse(kc, 10, 10);

    // Center plant area
    double plantSize = ringRadius_ * 0.4;
    double ratio = (selectedMinutes_ - 10) / 110.0;
    double growthScale = 0.3 + ratio * 0.7;

    QRectF plantRect(ringCenterX_ - plantSize * growthScale,
                     ringCenterY_ - plantSize * growthScale * 0.7,
                     plantSize * growthScale * 2,
                     plantSize * growthScale * 2);

    // Draw plant as colored shape that grows
    QColor plantColor;
    switch (plantType_) {
    case 0: plantColor = QColor("#4E9F3D"); break;
    case 1: plantColor = QColor("#2E7D32"); break;
    case 2: plantColor = QColor("#C2185B"); break;
    default: plantColor = QColor("#4E9F3D");
    }

    painter.setBrush(plantColor);
    painter.setPen(Qt::NoPen);

    if (ratio < 0.2) {
        painter.drawEllipse(plantRect.center(), 3, 3);
    } else if (ratio < 0.5) {
        double r = plantSize * 0.15 * growthScale;
        painter.drawEllipse(plantRect.adjusted(-r, -r, r, r));
    } else if (ratio < 0.8) {
        double trunkW = plantSize * 0.08 * growthScale;
        double trunkH = plantSize * 0.4 * growthScale;
        painter.drawRect(QRectF(plantRect.center().x() - trunkW,
                                plantRect.bottom() - trunkH,
                                trunkW * 2, trunkH));
        painter.drawEllipse(plantRect.adjusted(-10, -20, 10, 0));
    } else {
        double trunkW = plantSize * 0.1 * growthScale;
        double trunkH = plantSize * 0.5 * growthScale;
        painter.drawRect(QRectF(plantRect.center().x() - trunkW,
                                plantRect.bottom() - trunkH,
                                trunkW * 2, trunkH));
        painter.drawEllipse(plantRect);
    }

    // Time text in center
    uint32_t secs = displaySeconds_;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    QString timeStr = isRunning_
        ? QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QStringLiteral("%1 min").arg(selectedMinutes_);

    QFont timeFont("Microsoft YaHei", 18, QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor("#E8EAE6"));
    QRectF timeRect(0, ringCenterY_ + ringRadius_ * 0.5, width(), 30);
    painter.drawText(timeRect, Qt::AlignCenter, timeStr);

    // Oath text above
    if (!oath_.isEmpty()) {
        QFont oathFont("Microsoft YaHei", 12, QFont::Bold);
        painter.setFont(oathFont);
        painter.setPen(QColor("#8A9A86"));
        QRectF oathRect(0, ringCenterY_ - ringRadius_ - 35, width(), 25);
        painter.drawText(oathRect, Qt::AlignCenter, oath_);
    }
}
