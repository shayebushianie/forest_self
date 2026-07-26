#include "ui/GuardianDashboardWidget.h"
#include "config/PlantCatalog.h"
#include "ui/PlantImageUtils.h"

#include <QConicalGradient>
#include <QFontMetrics>
#include <QHideEvent>
#include <QLinearGradient>
#include <QPropertyAnimation>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStyle>
#include <QtMath>
#include <algorithm>
#include <cmath>

GuardianDashboardWidget::GuardianDashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(760, 620);
    setAutoFillBackground(false);
    plantIcon_ = PlantImageUtils::loadPlantIcon(PlantCatalog::byType(0).iconPath);

    goalSpin_ = new QSpinBox(this);
    goalSpin_->setObjectName("guardianGoalSpin");
    goalSpin_->setRange(10, 600);
    goalSpin_->setSuffix(QStringLiteral(" 分钟"));
    goalSpin_->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    goalSpin_->setToolTip(QStringLiteral("设置每日专注目标，修改后会立即保存。"));
    goalSpin_->setAccessibleName(QStringLiteral("每日专注目标"));
    goalSpin_->setStyleSheet(QStringLiteral(
        "QSpinBox { background:rgba(247,255,247,232); color:#2B6C54;"
        " border:1px solid rgba(255,255,255,160); border-radius:14px;"
        " padding:9px 14px; font-size:18px; font-weight:800;"
        " selection-background-color:rgba(94,167,111,110); selection-color:#174B3B; }"
        "QSpinBox QLineEdit { background:transparent; color:#2B6C54;"
        " selection-background-color:rgba(94,167,111,110); selection-color:#174B3B; }"
        "QSpinBox::up-button, QSpinBox::down-button { width:24px; border:none; background:transparent; }"));
    connect(goalSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (!syncingGoalSpin_) emit dailyGoalChanged(value);
    });

    setProperty("shownProgress", shownProgress_);
    syncGoalSpin();
}

void GuardianDashboardWidget::setSnapshot(uint32_t todayMinutes,
                                          uint32_t dailyGoalMinutes,
                                          uint32_t currentStreak,
                                          uint32_t longestStreak,
                                          uint32_t totalMinutes)
{
    todayMinutes_ = todayMinutes;
    dailyGoalMinutes_ = std::max<uint32_t>(1, dailyGoalMinutes);
    currentStreak_ = currentStreak;
    longestStreak_ = longestStreak;
    totalMinutes_ = totalMinutes;
    targetProgress_ = std::min<qreal>(1.0, static_cast<qreal>(todayMinutes_) / dailyGoalMinutes_);
    syncGoalSpin();
    if (progressAnimation_) {
        progressAnimation_->stop();
        progressAnimation_->deleteLater();
    }
    if (reducedMotion_ || !isVisible() || qFuzzyCompare(shownProgress_, targetProgress_)) {
        setShownProgress(targetProgress_);
        return;
    }
    auto* animation = new QPropertyAnimation(this, "shownProgress", this);
    progressAnimation_ = animation;
    animation->setDuration(200);
    animation->setStartValue(shownProgress_);
    animation->setEndValue(targetProgress_);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void GuardianDashboardWidget::setReducedMotion(bool reducedMotion)
{
    reducedMotion_ = reducedMotion;
    if (reducedMotion_ && progressAnimation_) {
        progressAnimation_->stop();
        progressAnimation_->deleteLater();
    }
    if (reducedMotion_) setShownProgress(targetProgress_);
}

void GuardianDashboardWidget::setShownProgress(qreal progress)
{
    shownProgress_ = std::clamp(progress, 0.0, 1.0);
    update();
}

void GuardianDashboardWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    drawBackground(painter);
    const auto layout = calculateLayout();
    drawHeader(painter, layout.header);
    drawHero(painter, layout.hero);
    drawStats(painter, layout.stats);
    drawGoalCard(painter, layout.goal);
    drawTimeline(painter, layout.timeline);
}

void GuardianDashboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncGoalSpin();
}

void GuardianDashboardWidget::hideEvent(QHideEvent* event)
{
    if (progressAnimation_) {
        progressAnimation_->stop();
        progressAnimation_->deleteLater();
    }
    setShownProgress(targetProgress_);
    QWidget::hideEvent(event);
}

GuardianDashboardWidget::LayoutRects GuardianDashboardWidget::calculateLayout() const
{
    const qreal w = width();
    const qreal h = height();
    const qreal m = 30.0;
    const qreal contentW = std::max<qreal>(320.0, w - m * 2.0);
    const qreal headerH = 66.0;
    const qreal gap = 18.0;
    const qreal bottomReserved = 240.0;
    const qreal heroH = std::clamp(h - m * 2.0 - headerH - bottomReserved, 230.0, 300.0);
    const qreal leftW = std::max<qreal>(430.0, contentW * 0.62);
    const qreal rightW = std::max<qreal>(220.0, contentW - leftW - gap);
    const qreal topY = m + headerH;
    const qreal goalY = topY + heroH + gap;
    const qreal goalH = 96.0;
    const qreal timelineY = goalY + goalH + gap;
    const qreal timelineH = std::max<qreal>(88.0, h - timelineY - m);

    return {
        QRectF(m, m, contentW, headerH - 8.0),
        QRectF(m, topY, leftW, heroH),
        QRectF(m + leftW + gap, topY, rightW, heroH),
        QRectF(m, goalY, contentW, goalH),
        QRectF(m, timelineY, contentW, timelineH)
    };
}

void GuardianDashboardWidget::syncGoalSpin()
{
    if (!goalSpin_) return;
    const auto layout = calculateLayout();
    const QRectF r = layout.goal;
    const int spinW = width() < 900 ? 150 : 176;
    const int spinH = 48;
    goalSpin_->setGeometry(static_cast<int>(r.right() - spinW - 28),
                           static_cast<int>(r.center().y() - spinH / 2.0),
                           spinW,
                           spinH);
    syncingGoalSpin_ = true;
    goalSpin_->setValue(static_cast<int>(dailyGoalMinutes_));
    syncingGoalSpin_ = false;
}

void GuardianDashboardWidget::drawBackground(QPainter& painter) const
{
    QLinearGradient bg(0, 0, width(), height());
    bg.setColorAt(0.0, QColor("#4EA889"));
    bg.setColorAt(0.55, QColor("#68BEA8"));
    bg.setColorAt(1.0, QColor("#9FD4DF"));
    painter.fillRect(rect(), bg);

    painter.setPen(QPen(QColor(255, 255, 255, 34), 1));
    for (int i = 0; i < 7; ++i) {
        const qreal y = 90 + i * 82;
        QPainterPath path;
        path.moveTo(-20, y);
        path.cubicTo(width() * 0.25, y - 20, width() * 0.55, y + 22, width() + 20, y - 10);
        painter.drawPath(path);
    }

    for (int i = 0; i < 12; ++i) {
        const qreal x = std::fmod(i * 143.0, width() + 60.0) - 30.0;
        const qreal y = 86.0 + std::fmod(i * 71.0, std::max<qreal>(120.0, height() - 140.0));
        painter.save();
        painter.translate(x, y);
        painter.rotate(-28);
        painter.setBrush(QColor(247, 255, 247, 42));
        painter.setPen(Qt::NoPen);
        QPainterPath leaf;
        leaf.moveTo(0, -8);
        leaf.cubicTo(14, -5, 16, 7, 0, 13);
        leaf.cubicTo(-14, 7, -12, -5, 0, -8);
        painter.drawPath(leaf);
        painter.restore();
    }
}

void GuardianDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect) const
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(43, 108, 84, 86));
    painter.drawRoundedRect(rect.adjusted(0, 0, 0, -6), 22, 22);

    QFont title = painter.font();
    title.setPointSize(22);
    title.setBold(true);
    painter.setFont(title);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(rect.adjusted(24, 0, 0, -6), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("⏰ 时间守护"));

    QFont small = painter.font();
    small.setPointSize(11);
    small.setBold(true);
    painter.setFont(small);
    painter.setPen(QColor(247, 255, 247, 190));
    const QString status = todayMinutes_ >= dailyGoalMinutes_
        ? QStringLiteral("今日已点亮")
        : QStringLiteral("今日进行中");
    painter.drawText(rect.adjusted(0, 0, 24, -6), Qt::AlignVCenter | Qt::AlignRight, status);
}

void GuardianDashboardWidget::drawHero(QPainter& painter, const QRectF& rect) const
{
    drawCard(painter, rect, 28);

    const qreal ringSize = std::clamp(std::min(rect.height() * 0.66, rect.width() * 0.26), 130.0, 190.0);
    const QPointF center(rect.left() + ringSize * 0.58 + 42.0, rect.center().y() + 2.0);
    const QRectF ring(center.x() - ringSize / 2.0, center.y() - ringSize / 2.0, ringSize, ringSize);
    const qreal pulse = 0.5;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(242, 244, 198, static_cast<int>(26 + pulse * 26)));
    painter.drawEllipse(ring.adjusted(-16 - pulse * 6, -16 - pulse * 6, 16 + pulse * 6, 16 + pulse * 6));

    QPen track(QColor(43, 108, 84, 72), 13, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(track);
    painter.drawArc(ring.adjusted(12, 12, -12, -12), 90 * 16, -360 * 16);

    QConicalGradient arcGrad(center, -90);
    arcGrad.setColorAt(0.0, QColor("#F2F4C6"));
    arcGrad.setColorAt(0.55, QColor("#72D7B2"));
    arcGrad.setColorAt(1.0, QColor("#F2F4C6"));
    QPen progressPen(QBrush(arcGrad), 13, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(progressPen);
    painter.drawArc(ring.adjusted(12, 12, -12, -12), 90 * 16,
                    static_cast<int>(-360.0 * 16.0 * shownProgress_));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(247, 255, 247, 230));
    painter.drawEllipse(ring.adjusted(30, 30, -30, -30));
    drawPlantIcon(painter, ring.adjusted(42, 38, -42, -34));

    const qreal pct = targetProgress_ * 100.0;
    const qreal textLeft = ring.right() + 48.0;
    const QRectF textRect(textLeft, rect.top() + 48.0,
                          rect.right() - textLeft - 34.0, rect.height() - 84.0);
    QFont big = painter.font();
    big.setPointSize(width() < 900 ? 25 : 31);
    big.setBold(true);
    painter.setFont(big);
    painter.setPen(QColor("#FFF8C8"));
    drawTextFit(painter, QRectF(textRect.left(), textRect.top(), textRect.width(), 44),
                QStringLiteral("%1 / %2 分钟").arg(todayMinutes_).arg(dailyGoalMinutes_),
                Qt::AlignLeft | Qt::AlignVCenter, 18);

    QFont mid = painter.font();
    mid.setPointSize(15);
    mid.setBold(true);
    painter.setFont(mid);
    painter.setPen(QColor(247, 255, 247, 215));
    painter.drawText(QRectF(textRect.left(), textRect.top() + 52, textRect.width(), 28),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("今日完成 %1%").arg(static_cast<int>(pct + 0.5)));

    const QRectF vine(textRect.left(), textRect.top() + 100, std::max<qreal>(120, textRect.width() - 10), 18);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(43, 108, 84, 92));
    painter.drawRoundedRect(vine, 9, 9);
    const qreal fillW = std::max<qreal>(18.0, vine.width() * shownProgress_);
    QLinearGradient fill(vine.left(), 0, vine.right(), 0);
    fill.setColorAt(0.0, QColor("#F2F4C6"));
    fill.setColorAt(0.55, QColor("#8CE1BC"));
    fill.setColorAt(1.0, QColor("#55B98F"));
    painter.setBrush(fill);
    painter.drawRoundedRect(QRectF(vine.left(), vine.top(), fillW, vine.height()), 9, 9);
    painter.setBrush(QColor(255, 255, 255, 70));
    const qreal shineX = vine.left() + vine.width() * 0.28;
    painter.drawRoundedRect(QRectF(shineX, vine.top() + 3, 54, vine.height() - 6), 7, 7);
}

void GuardianDashboardWidget::drawPlantIcon(QPainter& painter, const QRectF& rect) const
{
    painter.save();
    if (!plantIcon_.isNull()) {
        const QSize requestedSize = rect.size().toSize();
        if (scaledPlantIcon_.isNull() || scaledPlantSize_ != requestedSize) {
            scaledPlantIcon_ = plantIcon_.scaled(requestedSize, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation);
            scaledPlantSize_ = requestedSize;
        }
        const QPointF topLeft(rect.center().x() - scaledPlantIcon_.width() / 2.0,
                              rect.center().y() - scaledPlantIcon_.height() / 2.0);
        painter.drawPixmap(topLeft, scaledPlantIcon_);
    } else {
        const QIcon icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
        icon.paint(&painter, rect.toRect(), Qt::AlignCenter);
    }
    painter.restore();
}

void GuardianDashboardWidget::drawStats(QPainter& painter, const QRectF& rect) const
{
    const qreal gap = 14.0;
    const qreal cardH = (rect.height() - gap * 2.0) / 3.0;
    drawStatCard(painter, QRectF(rect.left(), rect.top(), rect.width(), cardH),
                 QStringLiteral("🔥"), QString::number(currentStreak_), QStringLiteral("当前连续"));
    drawStatCard(painter, QRectF(rect.left(), rect.top() + cardH + gap, rect.width(), cardH),
                 QStringLiteral("🏆"), QString::number(longestStreak_), QStringLiteral("最长连续"));
    drawStatCard(painter, QRectF(rect.left(), rect.top() + (cardH + gap) * 2.0, rect.width(), cardH),
                 QStringLiteral("⏱"), QString::number(totalMinutes_ / 60), QStringLiteral("累计小时"));
}

void GuardianDashboardWidget::drawGoalCard(QPainter& painter, const QRectF& rect) const
{
    drawCard(painter, rect, 24);
    QFont title = painter.font();
    title.setPointSize(21);
    title.setBold(true);
    painter.setFont(title);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(rect.adjusted(28, 0, -220, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("每日目标"));

    painter.setPen(Qt::NoPen);
    const qreal x0 = rect.left() + rect.width() * 0.33;
    const qreal stepGap = std::max<qreal>(38.0, rect.width() * 0.065);
    for (int i = 0; i < 5; ++i) {
        const bool active = shownProgress_ >= (i + 1) / 5.0;
        painter.setBrush(active ? QColor("#F2F4C6") : QColor(247, 255, 247, 82));
        const qreal y = rect.center().y();
        painter.drawEllipse(QPointF(x0 + i * stepGap, y), active ? 8 : 6, active ? 8 : 6);
    }
    painter.setPen(QPen(QColor(247, 255, 247, 68), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(x0, rect.center().y()), QPointF(x0 + 4 * stepGap, rect.center().y()));
}

void GuardianDashboardWidget::drawTimeline(QPainter& painter, const QRectF& rect) const
{
    drawCard(painter, rect, 24);
    QFont title = painter.font();
    title.setPointSize(15);
    title.setBold(true);
    painter.setFont(title);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(rect.adjusted(24, 12, -24, -12), Qt::AlignTop | Qt::AlignLeft,
                     QStringLiteral("守护节律"));

    const QRectF lane = rect.adjusted(30, 46, -30, -28);
    const int days = 7;
    const qreal gap = lane.width() / (days - 1);
    painter.setPen(QPen(QColor(247, 255, 247, 70), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(lane.left(), lane.center().y()), QPointF(lane.right(), lane.center().y()));
    for (int i = 0; i < days; ++i) {
        const bool active = i >= days - std::min<int>(days, static_cast<int>(currentStreak_));
        const QPointF p(lane.left() + gap * i, lane.center().y());
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? QColor("#F2F4C6") : QColor(247, 255, 247, 90));
        painter.drawEllipse(p, active ? 11 : 8, active ? 11 : 8);
        if (active) {
            painter.setBrush(QColor(94, 167, 111, 210));
            QPainterPath leaf;
            leaf.moveTo(p.x(), p.y() - 5);
            leaf.cubicTo(p.x() + 16, p.y() - 10, p.x() + 19, p.y() + 6, p.x() + 2, p.y() + 13);
            leaf.cubicTo(p.x() - 10, p.y() + 5, p.x() - 8, p.y() - 7, p.x(), p.y() - 5);
            painter.drawPath(leaf);
        }
    }
}

void GuardianDashboardWidget::drawCard(QPainter& painter, const QRectF& rect, qreal radius) const
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(43, 108, 84, 32));
    painter.drawRoundedRect(rect.translated(0, 6), radius, radius);

    QLinearGradient glass(rect.topLeft(), rect.bottomRight());
    glass.setColorAt(0.0, QColor(255, 255, 255, 88));
    glass.setColorAt(1.0, QColor(255, 255, 255, 36));
    painter.setBrush(glass);
    painter.setPen(QPen(QColor(247, 255, 247, 96), 1));
    painter.drawRoundedRect(rect, radius, radius);
}

void GuardianDashboardWidget::drawStatCard(QPainter& painter, const QRectF& rect, const QString& icon,
                                           const QString& value, const QString& title) const
{
    drawCard(painter, rect, 22);
    QFont iconFont = painter.font();
    iconFont.setPointSize(25);
    iconFont.setBold(false);
    painter.setFont(iconFont);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(QRectF(rect.left() + 18, rect.top(), 54, rect.height()),
                     Qt::AlignCenter, icon);

    QFont valueFont = painter.font();
    valueFont.setPointSize(28);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.setPen(QColor("#FFF8C8"));
    painter.drawText(QRectF(rect.left() + 78, rect.top() + 10, rect.width() - 96, rect.height() * 0.45),
                     Qt::AlignLeft | Qt::AlignVCenter, value);

    QFont titleFont = painter.font();
    titleFont.setPointSize(12);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(247, 255, 247, 185));
    painter.drawText(QRectF(rect.left() + 80, rect.center().y(), rect.width() - 96, rect.height() * 0.38),
                     Qt::AlignLeft | Qt::AlignVCenter, title);
}

void GuardianDashboardWidget::drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                                          int flags, int minPointSize) const
{
    QFont font = painter.font();
    int size = font.pointSize();
    while (size > minPointSize) {
        font.setPointSize(size);
        QFontMetrics metrics(font);
        if (metrics.boundingRect(rect.toRect(), flags | Qt::TextWordWrap, text).height() <= rect.height() &&
            metrics.horizontalAdvance(text) <= rect.width() + 12) {
            break;
        }
        --size;
    }
    painter.setFont(font);
    painter.drawText(rect, flags | Qt::TextWordWrap, text);
}
