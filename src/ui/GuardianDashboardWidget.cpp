#include "ui/AppStyle.h"
#include "ui/GuardianDashboardWidget.h"
#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include "ui/PlantImageUtils.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QSpinBox>
#include <QStyle>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kGoalSpinBasePixelSize = 18;
constexpr int kLargestFontScalePercent = 125;

QFont guardianFont(int pixelSize, bool bold = false)
{
    QFont font;
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    return AppStyle::scaledFont(font);
}

QString totalDurationText(uint32_t totalMinutes)
{
    const uint32_t hours = totalMinutes / 60;
    const uint32_t minutes = totalMinutes % 60;
    if (hours == 0) return QStringLiteral("%1 分钟").arg(static_cast<qulonglong>(totalMinutes));
    if (minutes == 0) return QStringLiteral("%1 小时").arg(static_cast<qulonglong>(hours));
    return QStringLiteral("%1 小时 %2 分钟").arg(static_cast<qulonglong>(hours))
        .arg(static_cast<qulonglong>(minutes));
}

} // namespace

GuardianDashboardWidget::GuardianDashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(760, 620);
    setAutoFillBackground(false);
    plantIcon_ = PlantImageUtils::loadPlantIcon(PlantCatalog::byType(PlantTypeId::Oak).iconPath);

    goalSpin_ = new QSpinBox(this);
    goalSpin_->setObjectName("guardianGoalSpin");
    goalSpin_->setRange(10, 600);
    goalSpin_->setSuffix(QStringLiteral(" 分钟"));
    goalSpin_->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    goalSpin_->setToolTip(QStringLiteral("修改后立即保存并应用于今天；过去日期的目标和已获得的最长连续记录保留。"));
    goalSpin_->setAccessibleName(QStringLiteral("每日专注目标"));
    goalSpin_->setStyleSheet(QStringLiteral(
        "QSpinBox { background:#FFFFFF; color:#203C35;"
        " border:1px solid #D5DFDA; border-radius:8px;"
        " padding:5px 10px; font-size:18px; font-weight:700;"
        " selection-background-color:#C9F0DE; selection-color:#203C35; }"
        "QSpinBox QLineEdit { background:transparent; color:#203C35;"
        " selection-background-color:#C9F0DE; selection-color:#203C35; }"
        "QSpinBox:focus { border:2px solid #246B57; }"));
    connect(goalSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (!syncingGoalSpin_) emit dailyGoalChanged(value);
    });

    connect(&animationTimer_, &QTimer::timeout, this, [this]() {
        if (!isVisible() || UserPreferences::instance().accessibilityOptions().reducedMotion) {
            shownProgress_ = targetProgress_;
            animationTimer_.stop();
        }
        shownProgress_ += (targetProgress_ - shownProgress_) * 0.08;
        if (qAbs(shownProgress_ - targetProgress_) < 0.001) {
            shownProgress_ = targetProgress_;
            animationTimer_.stop();
        }
        update();
    });
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
    if (isVisible() && !UserPreferences::instance().accessibilityOptions().reducedMotion &&
        qAbs(shownProgress_ - targetProgress_) >= 0.001) {
        animationTimer_.start(33);
    } else {
        shownProgress_ = targetProgress_;
        animationTimer_.stop();
    }
    syncGoalSpin();
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
    drawStats(painter, layout.stats, layout.goalInput, layout.statsContentTop);
    drawTimeline(painter, layout.timeline);
}

void GuardianDashboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    syncGoalSpin();
}

GuardianDashboardWidget::LayoutRects GuardianDashboardWidget::calculateLayout() const
{
    const qreal w = width();
    const qreal contentW = std::max<qreal>(0.0, std::min<qreal>(1160.0, w - 56.0));
    const qreal m = (w - contentW) / 2.0;
    const qreal headerH = QFontMetricsF(guardianFont(22, true)).height() + 4.0;
    const qreal gap = 16.0;
    const qreal topY = 24.0 + headerH + 16.0;
    const qreal timelineH = std::max<qreal>(
        124.0, std::max(QFontMetricsF(guardianFont(16, true)).height(),
                        QFontMetricsF(guardianFont(14)).height()) + 96.0);
    const qreal availableHeroH = height() - topY - gap - timelineH - 24.0;
    const qreal heroH = std::min<qreal>(availableHeroH, std::max<qreal>(320.0, height() * 0.62));
    const qreal leftW = std::max<qreal>(380.0, (contentW - gap) * 0.63);
    const qreal rightW = std::max<qreal>(0.0, contentW - leftW - gap);

    const QRectF statsRect(m + leftW + gap, topY, rightW, heroH);
    const qreal titleH = QFontMetricsF(guardianFont(16, true)).height();
    const qreal rowH = std::max(QFontMetricsF(guardianFont(14)).height(),
                                QFontMetricsF(guardianFont(17, true)).height()) + 8.0;
    QFont spinFont = goalSpin_ ? goalSpin_->font() : QFont();
    spinFont.setPixelSize(static_cast<int>(std::ceil(
        kGoalSpinBasePixelSize * kLargestFontScalePercent / 100.0)));
    spinFont.setBold(true);
    const QFontMetricsF spinMetrics(spinFont);
    const qreal spinW = spinMetrics.horizontalAdvance(QStringLiteral("600 分钟")) + 24.0 + 20.0 + 8.0;
    const qreal spinH = spinMetrics.height() + 10.0 + 4.0;
    const qreal statsBlockH = 18.0 + titleH + 10.0 + rowH * 3.0 + 12.0 + spinH + 18.0;
    const qreal statsContentTop = statsRect.top() + std::max<qreal>(18.0, (heroH - statsBlockH) / 2.0);
    const qreal goalY = statsContentTop + 18.0 + titleH + 10.0 + rowH * 3.0 + 12.0;
    const QRectF goalInput(statsRect.right() - 18.0 - spinW, goalY, spinW, spinH);

    return {
        QRectF(m, 24.0, contentW, headerH),
        QRectF(m, topY, leftW, heroH),
        statsRect,
        QRectF(m, topY + heroH + gap, contentW, timelineH),
        goalInput,
        statsContentTop
    };
}

void GuardianDashboardWidget::syncGoalSpin()
{
    if (!goalSpin_) return;
    const auto layout = calculateLayout();
    goalSpin_->setGeometry(layout.goalInput.toAlignedRect());
    syncingGoalSpin_ = true;
    goalSpin_->setValue(static_cast<int>(dailyGoalMinutes_));
    syncingGoalSpin_ = false;
}

void GuardianDashboardWidget::drawBackground(QPainter& painter) const
{
    painter.fillRect(rect(), AppStyle::color(AppStyle::Color::Workspace));
}

void GuardianDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect) const
{
    const QString status = todayMinutes_ >= dailyGoalMinutes_
        ? QStringLiteral("今日已达标")
        : todayMinutes_ == 0 ? QStringLiteral("今日尚未开始") : QStringLiteral("今日进行中");
    const qreal statusWidth = QFontMetricsF(guardianFont(14)).horizontalAdvance(status) + 8.0;
    const QRectF titleRect(rect.left(), rect.top(), rect.width() - statusWidth - 18.0, rect.height());
    const QRectF statusRect(rect.right() - statusWidth, rect.top(), statusWidth, rect.height());

    painter.setFont(guardianFont(22, true));
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("时间守护"));

    painter.setPen(AppStyle::color(AppStyle::Color::Primary));
    drawTextFit(painter, statusRect, status, Qt::AlignVCenter | Qt::AlignRight, 14, 11);
}

void GuardianDashboardWidget::drawHero(QPainter& painter, const QRectF& rect) const
{
    drawCard(painter, rect);

    const qreal ringSize = std::clamp(std::min(rect.height() * 0.58, rect.width() * 0.30), 132.0, 190.0);
    const QPointF center(rect.left() + 24.0 + ringSize / 2.0, rect.center().y());
    const QRectF ring(center.x() - ringSize / 2.0, center.y() - ringSize / 2.0, ringSize, ringSize);
    QPen track(AppStyle::color(AppStyle::Color::Border), 12, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(track);
    painter.drawArc(ring.adjusted(7, 7, -7, -7), 90 * 16, -360 * 16);

    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Primary), 12, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(ring.adjusted(7, 7, -7, -7), 90 * 16,
                    static_cast<int>(-360.0 * 16.0 * shownProgress_));

    painter.setPen(Qt::NoPen);
    painter.setBrush(AppStyle::color(AppStyle::Color::Workspace));
    painter.drawEllipse(ring.adjusted(25, 25, -25, -25));
    drawPlantIcon(painter, ring.adjusted(34, 34, -34, -34));

    const qreal pct = targetProgress_ * 100.0;
    const qreal textLeft = ring.right() + 28.0;
    const QRectF textRect(textLeft, rect.top() + 24.0,
                          std::max<qreal>(0.0, rect.right() - 24.0 - textLeft), rect.height() - 48.0);
    const QFont eyebrowFont = guardianFont(14, true);
    const QFont valueFont = guardianFont(28, true);
    const QFont detailFont = guardianFont(16);
    const qreal eyebrowH = QFontMetricsF(eyebrowFont).height();
    const qreal valueH = QFontMetricsF(valueFont).height();
    const qreal detailH = QFontMetricsF(detailFont).height();
    const qreal groupH = eyebrowH + 8.0 + valueH + 8.0 + detailH;
    qreal textY = textRect.center().y() - groupH / 2.0;

    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    drawTextFit(painter, QRectF(textRect.left(), textY, textRect.width(), eyebrowH + 2.0),
                QStringLiteral("今日进度"), Qt::AlignLeft | Qt::AlignVCenter, 14, 11, true);
    textY += eyebrowH + 8.0;

    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    drawTextFit(painter, QRectF(textRect.left(), textY, textRect.width(), valueH + 2.0),
                QStringLiteral("%1 / %2 分钟").arg(todayMinutes_).arg(dailyGoalMinutes_),
                Qt::AlignLeft | Qt::AlignVCenter, 28, 11, true);
    textY += valueH + 8.0;

    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    drawTextFit(painter, QRectF(textRect.left(), textY, textRect.width(), detailH + 2.0),
                QStringLiteral("今日完成 %1%").arg(static_cast<int>(pct + 0.5)),
                Qt::AlignLeft | Qt::AlignVCenter, 16, 11);
}

void GuardianDashboardWidget::drawPlantIcon(QPainter& painter, const QRectF& rect) const
{
    painter.save();
    if (!plantIcon_.isNull()) {
        const QPixmap scaled = plantIcon_.scaled(rect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPointF topLeft(rect.center().x() - scaled.width() / 2.0,
                              rect.center().y() - scaled.height() / 2.0);
        painter.drawPixmap(topLeft, scaled);
    } else {
        const QIcon icon = style()->standardIcon(QStyle::SP_DialogApplyButton);
        icon.paint(&painter, rect.toRect(), Qt::AlignCenter);
    }
    painter.restore();
}

void GuardianDashboardWidget::drawStats(QPainter& painter, const QRectF& rect,
                                         const QRectF& goalInput, qreal contentTop) const
{
    drawCard(painter, rect);

    constexpr qreal inset = 18.0;
    const QFont titleFont = guardianFont(16, true);
    const QFont labelFont = guardianFont(14);
    const QFont valueFont = guardianFont(17, true);
    const qreal titleH = QFontMetricsF(titleFont).height();
    const qreal rowH = std::max(QFontMetricsF(labelFont).height(),
                                QFontMetricsF(valueFont).height()) + 8.0;
    const qreal left = rect.left() + inset;
    const qreal innerW = rect.width() - inset * 2.0;

    painter.setFont(titleFont);
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.drawText(QRectF(left, contentTop + inset, innerW, titleH),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("连续记录"));

    qreal rowY = contentTop + inset + titleH + 10.0;
    const qreal labelW = innerW * 0.42;
    const struct {
        QString label;
        QString value;
    } rows[] = {
        {QStringLiteral("当前连续"), QStringLiteral("%1 天").arg(static_cast<qulonglong>(currentStreak_))},
        {QStringLiteral("最长记录"), QStringLiteral("%1 天").arg(static_cast<qulonglong>(longestStreak_))},
        {QStringLiteral("累计专注"), totalDurationText(totalMinutes_)}
    };

    for (int i = 0; i < 3; ++i) {
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        drawTextFit(painter, QRectF(left, rowY, labelW, rowH), rows[i].label,
                    Qt::AlignLeft | Qt::AlignVCenter, 14, 10);
        painter.setPen(AppStyle::color(AppStyle::Color::Text));
        drawTextFit(painter, QRectF(left + labelW, rowY, innerW - labelW, rowH), rows[i].value,
                    Qt::AlignRight | Qt::AlignVCenter, 17, 10, true);
        rowY += rowH;
        if (i < 2) {
            painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
            painter.drawLine(QPointF(left, rowY - 4.0), QPointF(rect.right() - inset, rowY - 4.0));
        }
    }

    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    const QRectF goalLabel(left, goalInput.top(), std::max<qreal>(0.0, goalInput.left() - 10.0 - left),
                           goalInput.height());
    drawTextFit(painter, goalLabel, QStringLiteral("每日目标"),
                Qt::AlignLeft | Qt::AlignVCenter, 14, 10);
}

void GuardianDashboardWidget::drawTimeline(QPainter& painter, const QRectF& rect) const
{
    drawCard(painter, rect);
    constexpr qreal inset = 18.0;
    const QFont titleFont = guardianFont(16, true);
    const QFont summaryFont = guardianFont(14);
    const qreal titleH = QFontMetricsF(titleFont).height();
    const qreal summaryW = QFontMetricsF(summaryFont).horizontalAdvance(
        QStringLiteral("连续达标 000000 天")) + 8.0;
    const QRectF titleRect(rect.left() + inset, rect.top() + 14.0,
                           rect.width() - summaryW - inset * 2.0 - 12.0, titleH + 2.0);
    const QRectF summaryRect(rect.right() - inset - summaryW, rect.top() + 14.0,
                             summaryW, titleH + 2.0);
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    drawTextFit(painter, titleRect, QStringLiteral("守护节律"),
                Qt::AlignLeft | Qt::AlignVCenter, 16, 11, true);
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    drawTextFit(painter, summaryRect,
                currentStreak_ == 0 ? QStringLiteral("尚无连续达标记录")
                                    : QStringLiteral("连续达标 %1 天").arg(
                                          static_cast<qulonglong>(currentStreak_)),
                Qt::AlignRight | Qt::AlignVCenter, 14, 10);

    const int days = 7;
    const qreal laneLeft = rect.left() + 36.0;
    const qreal laneRight = rect.right() - 36.0;
    const qreal gap = (laneRight - laneLeft) / (days - 1);
    const qreal laneY = rect.top() + rect.height() * 0.68;
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(laneLeft, laneY), QPointF(laneRight, laneY));
    for (int i = 0; i < days; ++i) {
        const bool active = i >= days - std::min<int>(days, static_cast<int>(currentStreak_));
        const QPointF p(laneLeft + gap * i, laneY);
        painter.setPen(Qt::NoPen);
        painter.setBrush(active ? AppStyle::color(AppStyle::Color::Primary)
                                : AppStyle::color(AppStyle::Color::Border));
        painter.drawEllipse(p, active ? 9 : 6, active ? 9 : 6);
    }
}

void GuardianDashboardWidget::drawCard(QPainter& painter, const QRectF& rect, qreal radius) const
{
    painter.setBrush(AppStyle::color(AppStyle::Color::Surface));
    painter.setPen(QPen(AppStyle::color(AppStyle::Color::Border), 1));
    painter.drawRoundedRect(rect, radius, radius);
}

void GuardianDashboardWidget::drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                                          int flags, int basePixelSize, int minPixelSize,
                                          bool bold) const
{
    QFont chosenFont;
    bool foundFont = false;
    for (int size = basePixelSize; size >= minPixelSize; --size) {
        QFont baseFont;
        baseFont.setPixelSize(size);
        baseFont.setBold(bold);
        const QFont candidate = AppStyle::scaledFont(baseFont);
        const QFontMetricsF metrics(candidate);
        if (metrics.horizontalAdvance(text) <= rect.width() && metrics.height() <= rect.height()) {
            chosenFont = candidate;
            foundFont = true;
            break;
        }
    }
    if (!foundFont) {
        QFont baseFont;
        baseFont.setPixelSize(minPixelSize);
        baseFont.setBold(bold);
        chosenFont = AppStyle::scaledFont(baseFont);
    }
    painter.setFont(chosenFont);
    painter.drawText(rect, flags | Qt::TextSingleLine, text);
}
