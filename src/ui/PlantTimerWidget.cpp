#include "ui/AppStyle.h"
#include "ui/PlantTimerWidget.h"
#include "config/PlantCatalog.h"
#include "ui/PlantImageUtils.h"
#include <QAccessible>
#include <QFocusEvent>
#include <QKeyEvent>
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
    setMinimumSize(280, 360);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(QStringLiteral("专注主页计时器"));
    setAccessibleDescription(QStringLiteral("项目标签、植物和专注时长设置。按 Tab 浏览可操作区域。"));
    selectedMinutes_ = 25;
    knobAngle_ = minutesToAngle(selectedMinutes_);
}

QSize PlantTimerWidget::sizeHint() const
{
    return QSize(640, 700);
}

QSize PlantTimerWidget::minimumSizeHint() const
{
    return QSize(280, 360);
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
    normalizeKeyboardFocusArea();
    refreshAccessibleText();
    update();
}

void PlantTimerWidget::setQuote(const QString& quote) { quote_ = quote; update(); }
void PlantTimerWidget::setOath(const QString& oath) { oath_ = oath; update(); }
void PlantTimerWidget::setPlantType(uint32_t type) { plantType_ = type; update(); }
void PlantTimerWidget::setTagInfo(const QString& text, const QColor& color)
{
    tagText_ = text.trimmed().isEmpty() ? QStringLiteral("无标签") : text.trimmed();
    tagColor_ = color.isValid() ? color : QColor("#B8B6EA");
    refreshAccessibleText();
    update();
}

void PlantTimerWidget::setSelectedMinutes(uint32_t minutes)
{
    selectedMinutes_ = qBound<uint32_t>(10, minutes, 120);
    if (!isRunning_ && !isStopwatch_) displaySeconds_ = selectedMinutes_ * 60;
    knobAngle_ = minutesToAngle(selectedMinutes_);
    refreshAccessibleText();
    update();
}

void PlantTimerWidget::setSessionState(bool active, uint32_t growthStage)
{
    isRunning_ = active;
    sessionGrowthStage_ = growthStage;
    if (active) dragging_ = false;
    normalizeKeyboardFocusArea();
    refreshAccessibleText();
    update();
}

void PlantTimerWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        resizeEvent(nullptr);
        update();
    }
}

void PlantTimerWidget::resizeEvent(QResizeEvent*)
{
    const QFont timeFont = AppStyle::scaledFont(QFont("Segoe UI Light", 48, QFont::Light));
    const QFont tagFont = AppStyle::scaledFont(QFont("Microsoft YaHei", 12, QFont::DemiBold));
    const int tagHeight = qMax(38, QFontMetrics(tagFont).height() + 16);
    const int reservedBottom = 48 + tagHeight + 24 + qCeil(QFontMetricsF(timeFont).height()) + 20;
    int reservedTop = 32;
    int side = qMin(width() - 90, height() - reservedTop - reservedBottom);
    side = qBound(90, side, 420);
    ringRadius_ = side / 2.0;
    ringCenterX_ = width() / 2.0;
    double maxCenterY = height() - reservedBottom - ringRadius_;
    ringCenterY_ = qMin(ringRadius_ + reservedTop, maxCenterY);
    ringRect_ = QRectF(ringCenterX_ - ringRadius_, ringCenterY_ - ringRadius_,
                       ringRadius_ * 2, ringRadius_ * 2);
    tagRect_.setHeight(tagHeight);
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

int PlantTimerWidget::keyboardFocusAreaIndex(FocusArea area) const
{
    switch (area) {
    case FocusArea::Tag: return 0;
    case FocusArea::Plant: return isRunning_ ? -1 : 1;
    case FocusArea::TimeRing: return selectorVisible() ? 2 : -1;
    case FocusArea::None: return -1;
    }
    return -1;
}

int PlantTimerWidget::keyboardFocusAreaCount() const
{
    if (selectorVisible()) return 3;
    return isRunning_ ? 1 : 2;
}

PlantTimerWidget::FocusArea PlantTimerWidget::keyboardFocusAreaAt(int index) const
{
    if (index == 0) return FocusArea::Tag;
    if (index == 1 && !isRunning_) return FocusArea::Plant;
    if (index == 2 && selectorVisible()) return FocusArea::TimeRing;
    return FocusArea::None;
}

void PlantTimerWidget::setKeyboardFocusArea(FocusArea area)
{
    if (focusArea_ == area) {
        refreshAccessibleText();
        update();
        return;
    }
    focusArea_ = area;
    refreshAccessibleText();
    update();

    if (hasFocus() && area != FocusArea::None) {
        QAccessibleEvent event(this, QAccessible::Focus);
        QAccessible::updateAccessibility(&event);
    }
}

void PlantTimerWidget::normalizeKeyboardFocusArea()
{
    if (!hasFocus()) {
        refreshAccessibleText();
        return;
    }

    if (keyboardFocusAreaIndex(focusArea_) < 0) {
        setKeyboardFocusArea(FocusArea::Tag);
    }
}

void PlantTimerWidget::refreshAccessibleText()
{
    if (!hasFocus() || focusArea_ == FocusArea::None) {
        setAccessibleName(QStringLiteral("专注主页计时器"));
        setAccessibleDescription(QStringLiteral("当前项目：%1。当前选择：%2 分钟。按 Tab 浏览项目标签、植物和可用的时长设置。")
                                     .arg(tagText_).arg(selectedMinutes_));
        return;
    }

    switch (focusArea_) {
    case FocusArea::Tag:
        setAccessibleName(QStringLiteral("项目标签"));
        setAccessibleDescription(QStringLiteral("当前项目：%1。按 Enter 或空格打开项目总览筛选。")
                                     .arg(tagText_));
        break;
    case FocusArea::Plant:
        setAccessibleName(QStringLiteral("选择植物"));
        setAccessibleDescription(QStringLiteral("按 Enter 或空格打开已解锁植物选择器。"));
        break;
    case FocusArea::TimeRing:
        setAccessibleName(QStringLiteral("专注时长"));
        setAccessibleDescription(QStringLiteral("当前选择：%1 分钟。使用方向键每次调整一分钟。")
                                     .arg(selectedMinutes_));
        break;
    case FocusArea::None:
        break;
    }
}

void PlantTimerWidget::mousePressEvent(QMouseEvent* event)
{
    QPointF pos = event->position();
    if (tagRect_.contains(pos)) {
        setKeyboardFocusArea(FocusArea::Tag);
        emit sig_tagClicked();
        return;
    }
    if (isRunning_) return;
    if (selectorVisible() && isOnKnob(pos)) {
        setKeyboardFocusArea(FocusArea::TimeRing);
        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (plantRect_.contains(pos)) {
        setKeyboardFocusArea(FocusArea::Plant);
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
            refreshAccessibleText();
            update();
        }
        return;
    }

    if (tagRect_.contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(tagText_);
        return;
    }
    if (!isRunning_ && plantRect_.contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("选择已解锁植物"));
        return;
    }

    if (selectorVisible()) {
        setCursor(isOnKnob(pos) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        setToolTip(isOnKnob(pos) ? QStringLiteral("调整专注时长") : QString());
    } else {
        setCursor(Qt::ArrowCursor);
        setToolTip(QString());
    }
}

void PlantTimerWidget::mouseReleaseEvent(QMouseEvent*)
{
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
}

void PlantTimerWidget::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    if (event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason) {
        const int index = event->reason() == Qt::BacktabFocusReason ? keyboardFocusAreaCount() - 1 : 0;
        setKeyboardFocusArea(keyboardFocusAreaAt(index));
    } else {
        normalizeKeyboardFocusArea();
        refreshAccessibleText();
        update();
    }
}

void PlantTimerWidget::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    refreshAccessibleText();
    update();
}

void PlantTimerWidget::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
        event->key() == Qt::Key_Space) {
        if (focusArea_ == FocusArea::Tag) {
            emit sig_tagClicked();
            event->accept();
            return;
        }
        if (focusArea_ == FocusArea::Plant && !isRunning_) {
            emit sig_plantClicked();
            event->accept();
            return;
        }
        if (focusArea_ == FocusArea::TimeRing) {
            event->accept();
            return;
        }
    }

    if (focusArea_ == FocusArea::TimeRing && selectorVisible()) {
        int adjustment = 0;
        switch (event->key()) {
        case Qt::Key_Up:
        case Qt::Key_Right:
            adjustment = 1;
            break;
        case Qt::Key_Down:
        case Qt::Key_Left:
            adjustment = -1;
            break;
        default:
            break;
        }

        if (adjustment != 0) {
            const uint32_t previousMinutes = selectedMinutes_;
            const int requestedMinutes = static_cast<int>(selectedMinutes_) + adjustment;
            setSelectedMinutes(static_cast<uint32_t>(qBound(10, requestedMinutes, 120)));
            if (selectedMinutes_ != previousMinutes) emit sig_timeSelected(selectedMinutes_);
            event->accept();
            return;
        }
    }

    QWidget::keyPressEvent(event);
}

bool PlantTimerWidget::focusNextPrevChild(bool next)
{
    if (!hasFocus()) return QWidget::focusNextPrevChild(next);

    const int currentIndex = keyboardFocusAreaIndex(focusArea_);
    const int nextIndex = currentIndex < 0
        ? (next ? 0 : keyboardFocusAreaCount() - 1)
        : currentIndex + (next ? 1 : -1);
    if (nextIndex >= 0 && nextIndex < keyboardFocusAreaCount()) {
        setKeyboardFocusArea(keyboardFocusAreaAt(nextIndex));
        return true;
    }

    setKeyboardFocusArea(FocusArea::None);
    return QWidget::focusNextPrevChild(next);
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

    const bool showSelector = selectorVisible();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(20, 63, 54, 12));
    painter.drawEllipse(ringRect_.translated(0, 8));
    painter.setBrush(AppStyle::color(AppStyle::Color::Selection));
    painter.setPen(QPen(QColor("#B0D6C4"), 1));
    painter.drawEllipse(ringRect_);

    QRectF innerRect = showSelector ? ringRect_.adjusted(18, 18, -18, -18)
                                    : ringRect_.adjusted(22, 22, -22, -22);
    painter.setBrush(AppStyle::color(AppStyle::Color::Surface));
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(innerRect);

    if (showSelector) {
        QPen trackPen;
        trackPen.setColor(QColor("#BDDCCF"));
        trackPen.setWidth(6);
        trackPen.setCapStyle(Qt::RoundCap);
        painter.setPen(trackPen);
        painter.drawEllipse(ringRect_.adjusted(7, 7, -7, -7));

        if (knobAngle_ > 1) {
            QPen progressPen;
            progressPen.setColor(AppStyle::color(AppStyle::Color::Primary));
            progressPen.setWidth(6);
            progressPen.setCapStyle(Qt::RoundCap);
            painter.setPen(progressPen);
            painter.drawArc(ringRect_.adjusted(7, 7, -7, -7), 90 * 16,
                            -static_cast<int>(knobAngle_ * 16));
        }

        QPointF kc = knobCenter(knobAngle_);
        painter.setBrush(AppStyle::color(AppStyle::Color::Primary));
        QPen knobBorder;
        knobBorder.setColor(AppStyle::color(AppStyle::Color::Surface));
        knobBorder.setWidth(2);
        painter.setPen(knobBorder);
        painter.drawEllipse(kc, 14, 14);
        painter.setBrush(QColor(255, 255, 255, 85));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(kc.x() - 4, kc.y() - 5), 4, 4);
    }

    if (showSelector) {
        const QRectF guideRect(innerRect.center().x() - 94, innerRect.top() + innerRect.height() * 0.13,
                               188, 24);
        painter.setBrush(AppStyle::color(AppStyle::Color::Surface));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(guideRect, 12, 12);
        painter.setFont(AppStyle::scaledFont(QFont("Microsoft YaHei", 9, QFont::DemiBold)));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        painter.drawText(guideRect, Qt::AlignCenter, QStringLiteral("拖动光环调整时长"));
    }

    // Idle previews use the selected duration; active growth comes from the domain plant.
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
    if (isRunning_) stage = sessionGrowthStage_;

    // --- plant image (scaled to fill the ring interior, clipped to circle) ----
    {
        const QPixmap& pm = plantPixmap(plantType_, stage, isRunning_ && stage == 4);
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
    int m = secs / 60;
    int s = secs % 60;
    QString timeStr = (isRunning_ || isStopwatch_)
        ? QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QStringLiteral("%1:00").arg(selectedMinutes_, 2, 10, QChar('0'));

    const qreal tagTop = ringRect_.bottom() + 48;
    QFont tagFont = AppStyle::scaledFont(QFont("Microsoft YaHei", 12, QFont::DemiBold));
    QFontMetrics tagMetrics(tagFont);
    const qreal tagWidth = qBound<qreal>(124, tagMetrics.horizontalAdvance(tagText_) + 62,
                                       qMin<qreal>(320, width() - 24));
    tagRect_ = QRectF(ringCenterX_ - tagWidth / 2.0, tagTop, tagWidth,
                      qMax(38, tagMetrics.height() + 16));
    painter.setBrush(AppStyle::color(AppStyle::Color::Surface));
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    painter.drawRoundedRect(tagRect_, 8, 8);
    painter.setBrush(tagColor_);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(tagRect_.left() + 20, tagRect_.center().y()), 6, 6);
    painter.setFont(tagFont);
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    const QRectF tagTextRect = tagRect_.adjusted(34, 0, -12, 0);
    painter.drawText(tagTextRect, Qt::AlignCenter,
        tagMetrics.elidedText(tagText_, Qt::ElideRight, static_cast<int>(tagTextRect.width())));

    QFont timeFont = AppStyle::scaledFont(QFont("Segoe UI Light", 48, QFont::Light));
    painter.setFont(timeFont);
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    const QFontMetricsF timeMetrics(timeFont);
    const qreal timeHeight = timeMetrics.height() + 8;
    const qreal timeCardWidth = qMin<qreal>(width() - 24,
        qMax<qreal>(260, timeMetrics.horizontalAdvance(timeStr) + 80));
    timeCardRect_ = QRectF(ringCenterX_ - timeCardWidth / 2.0, tagRect_.bottom() + 24,
                           timeCardWidth, timeHeight);
    painter.setFont(timeFont);
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.drawText(timeCardRect_, Qt::AlignCenter, timeStr);

    QString topText = !oath_.isEmpty() ? oath_ : quote_;
    if (!topText.isEmpty()) {
        QFont oathFont = AppStyle::scaledFont(QFont("Microsoft YaHei", 12, QFont::Bold));
        painter.setFont(oathFont);
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        QRectF oathRect(0, ringCenterY_ - ringRadius_ - 34, width(), 25);
        painter.drawText(oathRect, Qt::AlignCenter,
            QFontMetrics(oathFont).elidedText(topText, Qt::ElideRight, width() - 24));
    }

    if (hasFocus() && focusArea_ != FocusArea::None) {
        QRectF focusRect;
        if (focusArea_ == FocusArea::Tag) {
            focusRect = tagRect_.adjusted(-3, -3, 3, 3);
        } else if (focusArea_ == FocusArea::Plant) {
            focusRect = innerRect.adjusted(4, 4, -4, -4);
        } else if (focusArea_ == FocusArea::TimeRing) {
            focusRect = ringRect_.adjusted(-6, -6, 6, 6);
        }

        if (!focusRect.isEmpty()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor("#28583E"), 4));
            if (focusArea_ == FocusArea::Tag) painter.drawRoundedRect(focusRect, 18, 18);
            else painter.drawEllipse(focusRect);
            painter.setPen(QPen(QColor("#FFFFFF"), 2));
            if (focusArea_ == FocusArea::Tag) painter.drawRoundedRect(focusRect, 18, 18);
            else painter.drawEllipse(focusRect);
        }
    }
}
