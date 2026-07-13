#include "ui/ForestDashboardWidget.h"
#include "ui/PlantImageUtils.h"
#include "config/PlantCatalog.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QImage>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {

QString finalPlantPath(uint32_t plantType)
{
    return PlantCatalog::byType(plantType).iconPath;
}

QPixmap grayscalePixmap(const QPixmap& source)
{
    QImage img = source.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QColor color = QColor::fromRgba(line[x]);
            const int gray = qGray(color.rgb());
            line[x] = qRgba(gray, gray, gray, color.alpha());
        }
    }
    return QPixmap::fromImage(img);
}

qreal normalizedHash(uint32_t seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<qreal>(seed % 10000) / 10000.0;
}

}

ForestDashboardWidget::ForestDashboardWidget(QWidget* parent)
    : QWidget(parent), selectedDate_(QDate::currentDate())
{
    setMinimumSize(900, 720);
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
}

void ForestDashboardWidget::setRecords(std::vector<FocusRecord> records)
{
    allRecords_ = std::move(records);
    setRecordFilter(recordFilter_);
}

void ForestDashboardWidget::setRecordFilter(const RecordFilter& filter)
{
    recordFilter_ = filter;
    records_.clear();
    records_.reserve(allRecords_.size());
    for (const FocusRecord& record : allRecords_) {
        if (recordFilter_.date.isValid() &&
            QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startTimestamp), Qt::LocalTime).date() != recordFilter_.date) {
            continue;
        }
        if (recordFilter_.tagId != std::numeric_limits<uint32_t>::max() && record.tagId != recordFilter_.tagId) continue;
        if (recordFilter_.plantType != std::numeric_limits<uint32_t>::max() && record.plantType != recordFilter_.plantType) continue;
        if (recordFilter_.terminalState == 0 && record.status != FocusRecordStatus::Success) continue;
        if (recordFilter_.terminalState == 1 && record.status != FocusRecordStatus::Abandoned) continue;
        records_.push_back(record);
    }
    statisticsIndex_.rebuild(records_);
    cachedData_.reset();
    update();
}

void ForestDashboardWidget::setTagNames(const QMap<uint32_t, QString>& tagNames)
{
    tagNames_ = tagNames;
    cachedData_.reset();
    update();
}

void ForestDashboardWidget::mousePressEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    for (auto it = islandPlantHitRects_.crbegin(); it != islandPlantHitRects_.crend(); ++it) {
        if (it->first.contains(pos)) {
            emit focusRecordRequested(it->second);
            return;
        }
    }
    for (int i = 0; i < periodTabRects_.size(); ++i) {
        if (!periodTabRects_[i].contains(pos)) continue;
        periodMode_ = static_cast<PeriodMode>(i);
        cachedData_.reset();
        update();
        return;
    }
    if (prevDateRect_.contains(pos)) {
        switch (periodMode_) {
        case PeriodMode::Day: selectedDate_ = selectedDate_.addDays(-1); break;
        case PeriodMode::Week: selectedDate_ = selectedDate_.addDays(-7); break;
        case PeriodMode::Month: selectedDate_ = selectedDate_.addMonths(-1); break;
        case PeriodMode::Year: selectedDate_ = selectedDate_.addYears(-1); break;
        }
        cachedData_.reset();
        update();
        return;
    }
    if (nextDateRect_.contains(pos)) {
        switch (periodMode_) {
        case PeriodMode::Day: selectedDate_ = selectedDate_.addDays(1); break;
        case PeriodMode::Week: selectedDate_ = selectedDate_.addDays(7); break;
        case PeriodMode::Month: selectedDate_ = selectedDate_.addMonths(1); break;
        case PeriodMode::Year: selectedDate_ = selectedDate_.addYears(1); break;
        }
        cachedData_.reset();
        update();
        return;
    }
    if (settingsRect_.contains(pos)) {
        emit settingsRequested();
        return;
    }
    if (overviewRect_.contains(pos)) {
        emit overviewRequested();
        return;
    }
    if (filtersRect_.contains(pos)) {
        emit filtersRequested();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ForestDashboardWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    const bool onPlant = std::any_of(islandPlantHitRects_.cbegin(), islandPlantHitRects_.cend(),
        [&pos](const auto& hit) { return hit.first.contains(pos); });
    const bool onAction = overviewRect_.contains(pos) || filtersRect_.contains(pos) ||
        settingsRect_.contains(pos) ||
        std::any_of(periodTabRects_.cbegin(), periodTabRects_.cend(),
                    [&pos](const QRectF& rect) { return rect.contains(pos); });
    setCursor(onPlant || onAction ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

const ForestDashboardWidget::DashboardData& ForestDashboardWidget::buildData() const
{
    if (!cachedData_) {
        cachedData_ = FocusStatisticsQuery::query(statisticsIndex_, tagNames_, selectedDate_,
            static_cast<FocusStatisticsQuery::Period>(periodMode_));
    }
    return *cachedData_;
}
void ForestDashboardWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bg(rect().topLeft(), rect().bottomLeft());
    bg.setColorAt(0.0, QColor("#2F7B62"));
    bg.setColorAt(0.34, QColor("#64BCA3"));
    bg.setColorAt(1.0, QColor("#90CEE3"));
    painter.fillRect(rect(), bg);

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    const DashboardData& data = buildData();

    const qreal margin = 26.0;
    QRectF content = QRectF(rect()).adjusted(margin, 22, -margin, -24);
    QRectF header(content.left(), content.top(), content.width(), 74);
    drawHeader(painter, header);

    qreal y = header.bottom() + 18;
    qreal topHeight = std::max<qreal>(265, std::min<qreal>(340, height() * 0.40));
    qreal gap = 18;
    qreal leftW = content.width() * 0.60;
    QRectF islandRect(content.left(), y, leftW, topHeight);
    QRectF overviewRect(islandRect.right() + gap, y, content.right() - islandRect.right() - gap, topHeight);
    drawIslandPanel(painter, islandRect, data);
    drawOverviewCards(painter, overviewRect, data);

    y += topHeight + 22;
    qreal bottomHeight = content.bottom() - y;
    qreal cardGap = 18;
    qreal firstW = (content.width() - cardGap * 2) * 0.42;
    qreal secondW = (content.width() - cardGap * 2) * 0.29;
    QRectF timeRect(content.left(), y, firstW, bottomHeight);
    QRectF projectRect(timeRect.right() + cardGap, y, secondW, bottomHeight);
    QRectF treeRect(projectRect.right() + cardGap, y, content.right() - projectRect.right() - cardGap, bottomHeight);
    drawTimeCard(painter, timeRect, data);
    drawProjectCard(painter, projectRect, data);
    drawTreeCard(painter, treeRect, data);
}

void ForestDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    periodTabRects_.clear();
    prevDateRect_ = QRectF();
    nextDateRect_ = QRectF();
    settingsRect_ = QRectF();
    overviewRect_ = QRectF();
    filtersRect_ = QRectF();

    overviewRect_ = QRectF(rect.left(), rect.top() + 8, 84, 42);
    painter.setBrush(QColor(255, 255, 255, 52));
    painter.setPen(QPen(QColor(255, 255, 255, 78), 1));
    painter.drawRoundedRect(overviewRect_, 16, 16);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 16, QFont::Bold));
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(overviewRect_, Qt::AlignCenter, QStringLiteral("总览"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
    painter.setPen(QColor(255, 255, 255, 185));
    const QRectF recordCountRect(rect.left(), overviewRect_.bottom() + 3,
                                 overviewRect_.width(), 18);
    painter.drawText(recordCountRect, Qt::AlignCenter, QStringLiteral("%1 条记录").arg(records_.size()));

    QRectF tabs(overviewRect_.right() + 12, rect.top() + 8, 228, 42);
    painter.setBrush(QColor(255, 255, 255, 48));
    painter.setPen(QPen(QColor(255, 255, 255, 72), 1));
    painter.drawRoundedRect(tabs, 21, 21);

    const QStringList names = {QStringLiteral("日"), QStringLiteral("周"),
                               QStringLiteral("月"), QStringLiteral("年")};
    const qreal tabW = tabs.width() / names.size();
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 13, QFont::Bold));
    const int activeIndex = static_cast<int>(periodMode_);
    for (int i = 0; i < names.size(); ++i) {
        QRectF tab(tabs.left() + tabW * i + 3, tabs.top() + 3, tabW - 6, tabs.height() - 6);
        periodTabRects_.push_back(tab);
        if (i == activeIndex) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#FFFFFF"));
            painter.drawRoundedRect(tab, 17, 17);
            painter.setPen(QColor("#2F6F5A"));
        } else {
            painter.setPen(QColor(255, 255, 255, 205));
        }
        painter.drawText(tab, Qt::AlignCenter, names[i]);
    }

    settingsRect_ = QRectF(rect.right() - 62, rect.top() + 9, 52, 40);
    filtersRect_ = QRectF(settingsRect_.left() - 70, rect.top() + 9, 60, 40);
    const qreal dateW = std::clamp(rect.width() * 0.28, 238.0, 340.0);
    qreal dateX = std::max(rect.center().x() - dateW / 2.0, tabs.right() + 24.0);
    if (dateX + dateW > filtersRect_.left() - 24.0) {
        dateX = filtersRect_.left() - 24.0 - dateW;
    }
    QRectF dateRect(dateX, rect.top() + 8, dateW, 42);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(43, 108, 84, 82));
    painter.drawRoundedRect(dateRect, 21, 21);
    painter.setPen(QColor("#F7FFF7"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 13, QFont::DemiBold));
    painter.drawText(QRectF(dateRect.left() + 45, dateRect.top(), dateRect.width() - 90, dateRect.height()),
                     Qt::AlignCenter,
                     FocusStatisticsQuery::periodLabel(
                         selectedDate_, static_cast<FocusStatisticsQuery::Period>(periodMode_)));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 23, QFont::Bold));
    prevDateRect_ = QRectF(dateRect.left() + 10, dateRect.top() - 2, 34, dateRect.height());
    nextDateRect_ = QRectF(dateRect.right() - 44, dateRect.top() - 2, 34, dateRect.height());
    painter.drawText(prevDateRect_, Qt::AlignCenter, QStringLiteral("<"));
    painter.drawText(nextDateRect_, Qt::AlignCenter, QStringLiteral(">"));

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
    painter.setPen(QPen(QColor(255, 255, 255, 65), 1));
    painter.setBrush(QColor(255, 255, 255, 42));
    painter.drawRoundedRect(settingsRect_, 16, 16);
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(settingsRect_, Qt::AlignCenter, QStringLiteral("设置"));
    painter.setPen(QPen(QColor(255, 255, 255, 65), 1));
    painter.setBrush(recordFilter_.isActive() ? QColor("#F2C94C") : QColor(255, 255, 255, 42));
    painter.drawRoundedRect(filtersRect_, 16, 16);
    painter.setPen(recordFilter_.isActive() ? QColor("#245543") : QColor("#F7FFF7"));
    painter.drawText(filtersRect_, Qt::AlignCenter, QStringLiteral("筛选"));
    painter.restore();
}

void ForestDashboardWidget::drawIslandPanel(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    painter.save();

    QRectF islandBounds = rect.adjusted(22, 10, -22, -24);
    QPointF center(islandBounds.center().x() - 8, islandBounds.center().y() - 6);
    const qreal w = std::min(islandBounds.width() * 0.80, 440.0);
    const qreal h = w * 0.52;
    const qreal topY = center.y() - h * 0.45;
    const qreal depth = h * 0.22;

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(25, 75, 65, 62));
    painter.drawEllipse(QRectF(center.x() - w * 0.55, topY + h * 0.59, w * 1.10, h * 0.40));

    QPolygonF top;
    top << QPointF(center.x(), topY)
        << QPointF(center.x() + w / 2, topY + h / 2)
        << QPointF(center.x(), topY + h)
        << QPointF(center.x() - w / 2, topY + h / 2);

    const auto curveTo = [](QPainterPath& path, const QPointF& control1,
                            const QPointF& control2, const QPointF& end) {
        path.cubicTo(control1, control2, end);
    };

    QPainterPath grassPath;
    grassPath.moveTo(top[0]);
    curveTo(grassPath, QPointF(center.x() + w * 0.12, topY + h * 0.015),
            QPointF(center.x() + w * 0.42, topY + h * 0.30), top[1]);
    curveTo(grassPath, QPointF(center.x() + w * 0.51, topY + h * 0.53),
            QPointF(center.x() + w * 0.16, topY + h * 0.92), top[2]);
    curveTo(grassPath, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.51, topY + h * 0.53), top[3]);
    curveTo(grassPath, QPointF(center.x() - w * 0.42, topY + h * 0.30),
            QPointF(center.x() - w * 0.12, topY + h * 0.015), top[0]);
    grassPath.closeSubpath();

    QPainterPath lowerLayer = grassPath;
    lowerLayer.translate(0, depth);
    painter.setBrush(QColor("#155747"));
    painter.setPen(Qt::NoPen);
    painter.drawPath(lowerLayer);

    QPainterPath sideLayer;
    sideLayer.moveTo(top[3]);
    curveTo(sideLayer, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.05, topY + h * 0.99), top[2]);
    curveTo(sideLayer, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    sideLayer.lineTo(top[1] + QPointF(0, depth));
    curveTo(sideLayer, QPointF(center.x() + w * 0.51, topY + h * 0.53 + depth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + depth),
            top[2] + QPointF(0, depth));
    curveTo(sideLayer, QPointF(center.x() - w * 0.16, topY + h * 0.92 + depth),
            QPointF(center.x() - w * 0.51, topY + h * 0.53 + depth),
            top[3] + QPointF(0, depth));
    sideLayer.closeSubpath();
    painter.setBrush(QColor("#207B55"));
    painter.drawPath(sideLayer);

    QPainterPath rightSoil;
    rightSoil.moveTo(top[2]);
    curveTo(rightSoil, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    rightSoil.lineTo(top[1] + QPointF(0, depth));
    curveTo(rightSoil, QPointF(center.x() + w * 0.51, topY + h * 0.53 + depth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + depth),
            top[2] + QPointF(0, depth));
    rightSoil.closeSubpath();
    painter.setBrush(QColor("#166347"));
    painter.drawPath(rightSoil);

    const qreal grassLipDepth = depth * 0.30;
    QPainterPath grassLip;
    grassLip.moveTo(top[3]);
    curveTo(grassLip, QPointF(center.x() - w * 0.16, topY + h * 0.92),
            QPointF(center.x() - w * 0.05, topY + h * 0.99), top[2]);
    curveTo(grassLip, QPointF(center.x() + w * 0.16, topY + h * 0.92),
            QPointF(center.x() + w * 0.51, topY + h * 0.53), top[1]);
    grassLip.lineTo(top[1] + QPointF(0, grassLipDepth));
    curveTo(grassLip, QPointF(center.x() + w * 0.51, topY + h * 0.53 + grassLipDepth),
            QPointF(center.x() + w * 0.16, topY + h * 0.92 + grassLipDepth),
            top[2] + QPointF(0, grassLipDepth));
    curveTo(grassLip, QPointF(center.x() - w * 0.16, topY + h * 0.92 + grassLipDepth),
            QPointF(center.x() - w * 0.51, topY + h * 0.53 + grassLipDepth),
            top[3] + QPointF(0, grassLipDepth));
    grassLip.closeSubpath();
    painter.setBrush(QColor("#63AA59"));
    painter.drawPath(grassLip);

    painter.save();
    painter.setPen(QPen(QColor("#135A43"), 1.5, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 15; ++i) {
        const qreal progress = (i + 0.5) / 15.0;
        const qreal x = top[3].x() + (top[1].x() - top[3].x()) * progress;
        const qreal arch = 1.0 - std::abs(progress * 2.0 - 1.0);
        const qreal y = topY + h * (0.50 + arch * 0.50) + depth * 0.64;
        painter.drawLine(QPointF(x - 3, y), QPointF(x + 2, y + 2));
    }
    painter.restore();

    painter.setBrush(QColor("#F7E9B8"));
    painter.setPen(QPen(QColor("#FFEFC7"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(grassPath);

    painter.save();
    painter.setClipPath(grassPath);

    const QVector<QPointF> ponds = {
        QPointF(center.x() - w * 0.26, topY + h * 0.44),
        QPointF(center.x() + w * 0.25, topY + h * 0.53),
        QPointF(center.x() - w * 0.04, topY + h * 0.73),
        QPointF(center.x() + w * 0.05, topY + h * 0.30)
    };
    for (int index = 0; index < ponds.size(); ++index) {
        drawPond(painter, ponds[index], w * (index == 3 ? 0.075 : 0.10), h * 0.09);
    }
    painter.setPen(QPen(QColor("#E8CC88"), 1.2, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 32; ++i) {
        const qreal x = center.x() + (normalizedHash(37u + i * 19u) - 0.5) * w * 0.82;
        const qreal y = topY + (0.16 + normalizedHash(91u + i * 29u) * 0.68) * h;
        painter.drawLine(QPointF(x - 2.5, y + 1.5), QPointF(x + 2.5, y - 1.5));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#E3D09B"));
    for (int i = 0; i < 8; ++i) {
        const qreal x = center.x() + (normalizedHash(313u + i * 71u) - 0.5) * w * 0.50;
        const qreal y = topY + (0.38 + normalizedHash(401u + i * 43u) * 0.34) * h;
        painter.drawEllipse(QRectF(x - 3, y - 1.8, 6, 3.6));
    }
    painter.restore();

    painter.save();
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < 12; ++i) {
        const qreal progress = (i + 0.35) / 12.0;
        const qreal x = top[3].x() + (top[1].x() - top[3].x()) * progress;
        const qreal arch = 1.0 - std::abs(progress * 2.0 - 1.0);
        const qreal y = topY + h * (0.50 + arch * 0.50) + depth * 0.43;
        painter.setBrush(i % 2 == 0 ? QColor("#3B9655") : QColor("#4FAA5A"));
        painter.drawEllipse(QRectF(x - 6, y - 3, 12, 7));
        painter.drawEllipse(QRectF(x - 2, y - 6, 8, 7));
        if (i % 3 == 1) {
            const QColor petal = i % 2 == 0 ? QColor("#FFF3C4") : QColor("#F3C64F");
            painter.setBrush(petal);
            painter.drawEllipse(QRectF(x - 2.5, y - 5.5, 5, 5));
            painter.drawEllipse(QRectF(x - 5.5, y - 2.5, 5, 5));
            painter.drawEllipse(QRectF(x + 0.5, y - 2.5, 5, 5));
            painter.setBrush(QColor("#D89A43"));
            painter.drawEllipse(QRectF(x - 1, y - 1, 2, 2));
        }
    }
    painter.restore();

    struct PositionedPlant {
        const IslandPlant* plant = nullptr;
        QPointF base;
        qreal scale = 1.0;
        qreal radiusX = 0.0;
        qreal radiusY = 0.0;
    };

    QVector<const IslandPlant*> placementOrder;
    placementOrder.reserve(data.islandPlants.size());
    for (const auto& plant : data.islandPlants) {
        placementOrder.push_back(&plant);
    }
    std::sort(placementOrder.begin(), placementOrder.end(),
              [](const IslandPlant* a, const IslandPlant* b) {
                  return a->placementSeed < b->placementSeed;
              });

    const qreal densityScale = std::clamp(
        std::sqrt(7.0 / std::max(7, static_cast<int>(data.islandPlants.size()))), 0.38, 1.0);
    QVector<PositionedPlant> positioned;
    positioned.reserve(placementOrder.size());

    auto islandBase = [&](qreal u, qreal v) {
        return QPointF(center.x() + (u - v) * w * 0.46,
                       topY + (u + v) * h * 0.50 + h * 0.02);
    };

    for (const IslandPlant* plant : placementOrder) {
        qreal resolvedScale = (w / 390.0) * plant->scale * densityScale;
        PositionedPlant candidate;
        candidate.plant = plant;
        bool placed = false;

        for (int pass = 0; pass < 3 && !placed; ++pass) {
            candidate.scale = resolvedScale;
            candidate.radiusX = 34.0 * resolvedScale;
            candidate.radiusY = 40.0 * resolvedScale;

            for (uint32_t attempt = 0; attempt < 96; ++attempt) {
                const uint32_t attemptSeed = plant->placementSeed
                    ^ ((attempt + 1u) * 0x9E3779B9u);
                const qreal u = attempt == 0 ? plant->u
                    : 0.18 + normalizedHash(attemptSeed) * 0.64;
                const qreal v = attempt == 0 ? plant->v
                    : 0.18 + normalizedHash(attemptSeed ^ 0x85EBCA6Bu) * 0.64;
                candidate.base = islandBase(u, v);

                bool overlaps = false;
                for (const auto& other : positioned) {
                    const qreal xLimit = candidate.radiusX + other.radiusX + w * 0.015;
                    const qreal yLimit = candidate.radiusY + other.radiusY + h * 0.015;
                    const qreal x = (candidate.base.x() - other.base.x()) / xLimit;
                    const qreal y = (candidate.base.y() - other.base.y()) / yLimit;
                    if (x * x + y * y < 1.0) {
                        overlaps = true;
                        break;
                    }
                }
                if (!overlaps) {
                    placed = true;
                    break;
                }
            }
            resolvedScale *= 0.84;
        }

        if (placed) {
            positioned.push_back(candidate);
        }
    }

    std::sort(positioned.begin(), positioned.end(),
              [](const PositionedPlant& a, const PositionedPlant& b) {
                  return a.base.y() < b.base.y();
              });

    islandPlantHitRects_.clear();
    // Keep plant images complete even when their canopies reach the island boundary.
    for (const auto& plant : positioned) {
        drawPlantImage(painter, plant.base, plant.scale,
                       plant.plant->plantType, plant.plant->abandoned);
        const qreal hitWidth = 92.0 * plant.scale;
        const qreal hitHeight = 126.0 * plant.scale;
        islandPlantHitRects_.push_back({
            QRectF(plant.base.x() - hitWidth * 0.5, plant.base.y() - hitHeight,
                   hitWidth, hitHeight),
            plant.plant->recordId
        });
    }

    painter.setPen(QPen(QColor(255, 255, 255, 150), 1));
    for (int i = 0; i < 10; ++i) {
        QPointF a(top[3].x() + 18 + i * (w - 40) / 10, top[2].y() + depth * 0.48);
        painter.drawLine(a, a + QPointF(5, -5));
    }

    QRectF stat1(rect.left() + 20, rect.bottom() - 60, 120, 34);
    QRectF stat2(stat1.right() + 10, stat1.top(), 130, 34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 48));
    painter.drawRoundedRect(stat1, 17, 17);
    painter.drawRoundedRect(stat2, 17, 17);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(stat1, Qt::AlignCenter, QStringLiteral("叶子 %1").arg(std::max(0, data.successCount * 2)));
    painter.drawText(stat2, Qt::AlignCenter, QStringLiteral("成长值 %1").arg(data.growthValue));

    painter.restore();
}

void ForestDashboardWidget::drawOverviewCards(QPainter& painter, const QRectF& rect,
                                               const DashboardData& data) const
{
    painter.save();
    const qreal gap = 12;
    const int columns = 2;
    const qreal cardW = (rect.width() - gap) / columns;
    const qreal cardH = (rect.height() - gap * 2) / 3;
    const QVector<QPair<QString, QString>> items = {
        {QStringLiteral("累计专注时长"), QStringLiteral("%1 分钟").arg(data.totalMinutes)},
        {QStringLiteral("专注次数"), QStringLiteral("%1 次").arg(data.focusCount)},
        {QStringLiteral("成功 / 放弃"), QStringLiteral("%1 / %2").arg(data.successCount).arg(data.abandonedCount)},
        {QStringLiteral("项目数量"), QStringLiteral("%1 项").arg(data.projectCount)},
        {QStringLiteral("常用树种"), data.topTree},
        {QStringLiteral("成长值"), QString::number(data.growthValue)},
    };

    for (int i = 0; i < items.size(); ++i) {
        const int row = i / columns;
        const int col = i % columns;
        QRectF card(rect.left() + col * (cardW + gap), rect.top() + row * (cardH + gap), cardW, cardH);
        drawCard(painter, card, 18);
        painter.setPen(QColor("#7B8E87"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
        painter.drawText(card.adjusted(16, 12, -14, -card.height() * 0.52),
                         Qt::AlignLeft | Qt::AlignVCenter, items[i].first);
        painter.setPen(i == 3 ? QColor("#E3A936") : QColor("#49A47E"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), i == 3 ? 16 : 20, QFont::Bold));
        drawTextFit(painter, card.adjusted(16, card.height() * 0.38, -14, -10), items[i].second,
                    Qt::AlignLeft | Qt::AlignVCenter, 11);
    }
    painter.restore();
}

void ForestDashboardWidget::drawTimeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(QColor("#27322F"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold));
    painter.drawText(rect.adjusted(22, 18, -22, -rect.height() + 56), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("专注时间统计"));
    painter.setPen(QColor("#C3CCC8"));
    painter.drawText(QRectF(rect.right() - 48, rect.top() + 18, 26, 26), Qt::AlignCenter, QStringLiteral("i"));

    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::DemiBold));
    painter.setPen(QColor("#73817D"));
    painter.drawText(rect.adjusted(22, 62, -22, -rect.height() + 92), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("累计专注时长："));
    painter.setPen(QColor("#4CAE86"));
    painter.drawText(rect.adjusted(126, 62, -22, -rect.height() + 92), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 分钟").arg(data.totalMinutes));

    QRectF chart = rect.adjusted(36, 112, -28, -116);
    painter.setPen(QPen(QColor("#E3E9E6"), 1));
    const int maxVal = std::max(30, data.timeBuckets.isEmpty()
        ? 30
        : *std::max_element(data.timeBuckets.cbegin(), data.timeBuckets.cend()));
    for (int i = 0; i <= 3; ++i) {
        qreal y = chart.bottom() - chart.height() * i / 3.0;
        painter.drawLine(QPointF(chart.left(), y), QPointF(chart.right(), y));
        painter.setPen(QColor("#A2AAA6"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
        painter.drawText(QRectF(rect.left() + 8, y - 10, 25, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(maxVal * i / 3));
        painter.setPen(QPen(QColor("#E3E9E6"), 1));
    }

    if (!data.hasData) {
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::DemiBold));
        painter.setPen(QColor("#9AA8A2"));
        painter.drawText(chart, Qt::AlignCenter, QStringLiteral("当前时间范围暂无专注记录"));
    } else {
        const qreal barGap = data.timeBuckets.size() > 14 ? 3 : 7;
        const qreal barW = (chart.width() - barGap * (data.timeBuckets.size() - 1)) / data.timeBuckets.size();
        for (int i = 0; i < data.timeBuckets.size(); ++i) {
            qreal h = chart.height() * data.timeBuckets[i] / maxVal;
            QRectF bar(chart.left() + i * (barW + barGap), chart.bottom() - h, barW, h);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor("#4FAE8B"));
            painter.drawRoundedRect(bar, 4, 4);
        }
    }

    painter.setPen(QColor("#8D9994"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8));
    for (int i = 0; i < data.axisLabels.size(); ++i) {
        qreal x = data.axisLabels.size() == 1
            ? chart.center().x()
            : chart.left() + chart.width() * i / (data.axisLabels.size() - 1);
        painter.drawText(QRectF(x - 24, chart.bottom() + 8, 48, 18), Qt::AlignCenter, data.axisLabels[i]);
    }

    QRectF eventsRect(rect.left() + 22, rect.bottom() - 82, rect.width() - 44, 58);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
    painter.setPen(QColor("#56635F"));
    painter.drawText(QRectF(eventsRect.left(), eventsRect.top(), eventsRect.width(), 18),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("最近专注记录"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold));
    if (data.recentEvents.isEmpty()) {
        painter.setPen(QColor("#9AA8A2"));
        painter.drawText(eventsRect.adjusted(0, 22, 0, 0), Qt::AlignLeft | Qt::AlignTop,
                         QStringLiteral("暂无成功或放弃记录"));
    } else {
        qreal eventY = eventsRect.top() + 22;
        const int maxRows = std::min(2, static_cast<int>(data.recentEvents.size()));
        for (int i = 0; i < maxRows; ++i) {
            const auto& event = data.recentEvents[i];
            painter.setPen(event.color);
            painter.drawText(QRectF(eventsRect.left(), eventY, 44, 18),
                             Qt::AlignLeft | Qt::AlignVCenter, event.statusText);
            painter.setPen(QColor("#56635F"));
            painter.drawText(QRectF(eventsRect.left() + 48, eventY, eventsRect.width() - 128, 18),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             QStringLiteral("%1  %2").arg(event.timeText, event.projectName));
            painter.setPen(QColor("#9AA8A2"));
            eventY += 18;
        }
    }
    painter.restore();
}

void ForestDashboardWidget::drawProjectCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(QColor("#27322F"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold));
    painter.drawText(rect.adjusted(22, 18, -22, -rect.height() + 56), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("专注项目分布"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9, QFont::DemiBold));
    painter.setPen(QColor("#7E8D88"));
    painter.drawText(rect.adjusted(22, 48, -22, -rect.height() + 76), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("按项目标签使用次数统计"));

    QPointF center(rect.center().x(), rect.top() + rect.height() * 0.34);
    qreal size = std::min(rect.width() * 0.55, rect.height() * 0.30);
    QRectF donut(center.x() - size / 2, center.y() - size / 2, size, size);
    if (!data.hasData || data.projects.isEmpty()) {
        painter.setPen(QPen(QColor("#E7EEE9"), size * 0.18, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(donut, 90 * 16, -360 * 16);
    } else {
        int start = 90 * 16;
        for (const auto& slice : data.projects) {
            int span = static_cast<int>(-slice.ratio * 360 * 16);
            painter.setPen(QPen(slice.color, size * 0.18, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(donut, start, span);
            start += span;
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#F7FAF8"));
    painter.drawEllipse(donut.adjusted(size * 0.24, size * 0.24, -size * 0.24, -size * 0.24));

    qreal y = rect.top() + rect.height() * 0.52;
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
    if (!data.hasData || data.projects.isEmpty()) {
        painter.setPen(QColor("#9AA8A2"));
        painter.drawText(QRectF(rect.left() + 24, y, rect.width() - 48, 24),
                         Qt::AlignCenter, QStringLiteral("暂无项目分布"));
        painter.restore();
        return;
    }
    for (const auto& slice : data.projects) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(slice.color);
        painter.drawEllipse(QRectF(rect.left() + 24, y + 4, 10, 10));
        painter.setPen(QColor("#56635F"));
        const QRectF nameRect(rect.left() + 42, y, rect.width() - 112, 16);
        drawTextFit(painter, nameRect, slice.name, Qt::AlignLeft | Qt::AlignVCenter, 8);
        painter.setPen(QColor("#7B8883"));
        painter.drawText(QRectF(rect.right() - 62, y, 44, 16), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 次").arg(slice.totalCount));
        const int successPercent = slice.totalCount > 0
            ? static_cast<int>(slice.successCount * 100.0 / slice.totalCount + 0.5)
            : 0;
        const int abandonedPercent = slice.totalCount > 0
            ? static_cast<int>(slice.abandonedCount * 100.0 / slice.totalCount + 0.5)
            : 0;
        const qreal metricLeft = rect.left() + 42;
        const qreal metricWidth = rect.width() - 60;
        const qreal metricGap = 8;
        const qreal metricColumnWidth = (metricWidth - metricGap) / 2.0;
        painter.setPen(QColor("#4FAE8B"));
        drawTextFit(painter,
                    QRectF(metricLeft, y + 16, metricColumnWidth, 15),
                    QStringLiteral("成功 %1%").arg(successPercent),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    8);
        painter.setPen(QColor("#B98A5A"));
        drawTextFit(painter,
                    QRectF(metricLeft + metricColumnWidth + metricGap, y + 16,
                           metricColumnWidth, 15),
                    QStringLiteral("放弃 %1%").arg(abandonedPercent),
                    Qt::AlignRight | Qt::AlignVCenter,
                    8);
        y += 31;
    }
    painter.restore();
}

void ForestDashboardWidget::drawTreeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const
{
    drawCard(painter, rect);
    painter.save();
    painter.setPen(QColor("#27322F"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 17, QFont::Bold));
    painter.drawText(rect.adjusted(22, 18, -22, -rect.height() + 56), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("常用树种"));

    if (!data.hasData || data.trees.isEmpty()) {
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::DemiBold));
        painter.setPen(QColor("#9AA8A2"));
        painter.drawText(rect.adjusted(22, 74, -22, -22), Qt::AlignCenter,
                         QStringLiteral("暂无树种记录"));
        painter.restore();
        return;
    }

    const int maxCount = data.trees.isEmpty() ? 1 : std::max(1, data.trees.first().count);
    qreal y = rect.top() + 78;
    for (int i = 0; i < data.trees.size(); ++i) {
        const auto& tree = data.trees[i];
        QRectF thumb(rect.left() + 22, y, 48, 54);
        drawPlantImage(painter, QPointF(thumb.center().x(), thumb.bottom() - 4),
                       0.68, tree.plantType, false);

        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
        painter.setPen(QColor("#4A5652"));
        painter.drawText(QRectF(rect.left() + 78, y + 2, rect.width() - 110, 22), Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("%1. %2").arg(i + 1).arg(tree.name));

        QRectF barBg(rect.left() + 78, y + 32, rect.width() - 128, 12);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#E7EEE9"));
        painter.drawRoundedRect(barBg, 6, 6);
        QRectF bar = barBg;
        bar.setWidth(barBg.width() * tree.count / maxCount);
        painter.setBrush(QColor("#58AD8F"));
        painter.drawRoundedRect(bar, 6, 6);

        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
        painter.setPen(QColor("#7B8883"));
        painter.drawText(QRectF(rect.right() - 42, y + 24, 28, 22), Qt::AlignRight | Qt::AlignVCenter,
                         QStringLiteral("%1 次").arg(tree.count));
        y += 68;
    }
    painter.restore();
}

void ForestDashboardWidget::drawCard(QPainter& painter, const QRectF& rect, qreal radius) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(33, 75, 66, 26));
    painter.drawRoundedRect(rect.translated(0, 5), radius, radius);
    painter.setBrush(QColor(255, 255, 255, 238));
    painter.drawRoundedRect(rect, radius, radius);
    painter.setPen(QPen(QColor(255, 255, 255, 150), 1));
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    painter.restore();
}

void ForestDashboardWidget::drawTree(QPainter& painter, QPointF base, qreal scale, const QColor& crown) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#8A5A33"));
    QRectF trunk(base.x() - 4 * scale, base.y() - 34 * scale, 8 * scale, 34 * scale);
    painter.drawRoundedRect(trunk, 3 * scale, 3 * scale);

    QPolygonF crownShape;
    crownShape << QPointF(base.x(), base.y() - 72 * scale)
               << QPointF(base.x() + 26 * scale, base.y() - 30 * scale)
               << QPointF(base.x(), base.y() - 14 * scale)
               << QPointF(base.x() - 25 * scale, base.y() - 30 * scale);
    painter.setBrush(crown);
    painter.drawPolygon(crownShape);
    painter.setBrush(crown.lighter(114));
    painter.drawEllipse(QRectF(base.x() - 15 * scale, base.y() - 60 * scale, 26 * scale, 22 * scale));
    painter.restore();
}

void ForestDashboardWidget::drawWitheredTree(QPainter& painter, QPointF base, qreal scale) const
{
    painter.save();
    painter.setPen(Qt::NoPen);

    painter.setBrush(QColor(76, 59, 43, 150));
    painter.drawEllipse(QRectF(base.x() - 18 * scale, base.y() - 8 * scale,
                               36 * scale, 12 * scale));

    QPen trunkPen(QColor("#7B5638"), 7 * scale, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(trunkPen);
    painter.drawLine(QPointF(base.x() - 3 * scale, base.y() - 2 * scale),
                     QPointF(base.x() + 5 * scale, base.y() - 42 * scale));

    QPen branchPen(QColor("#6E4A31"), 4 * scale, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(branchPen);
    QPointF joint(base.x() + 3 * scale, base.y() - 30 * scale);
    painter.drawLine(joint, joint + QPointF(-18 * scale, -12 * scale));
    painter.drawLine(joint, joint + QPointF(16 * scale, -10 * scale));
    painter.drawLine(joint + QPointF(-8 * scale, -6 * scale),
                     joint + QPointF(-20 * scale, 2 * scale));

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#8E755E"));
    painter.drawEllipse(QRectF(base.x() - 8 * scale, base.y() - 54 * scale,
                               14 * scale, 10 * scale));
    painter.restore();
}

void ForestDashboardWidget::drawPlantImage(QPainter& painter, QPointF base, qreal scale,
                                           uint32_t plantType, bool abandoned) const
{
    QPixmap pm = PlantImageUtils::loadPlantIcon(finalPlantPath(plantType));
    if (pm.isNull()) {
        if (abandoned) drawWitheredTree(painter, base, scale);
        else drawTree(painter, base, scale, PlantCatalog::byType(plantType).chartColor);
        return;
    }
    if (abandoned) {
        pm = grayscalePixmap(pm);
    }

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(abandoned ? QColor(48, 48, 48, 70) : QColor(38, 94, 74, 58));
    painter.drawEllipse(QRectF(base.x() - 23 * scale, base.y() - 8 * scale,
                               46 * scale, 14 * scale));

    const QSizeF targetSize = pm.size().scaled(58 * scale, 72 * scale, Qt::KeepAspectRatio);
    QRectF target(base.x() - targetSize.width() / 2,
                  base.y() - targetSize.height() + 4 * scale,
                  targetSize.width(),
                  targetSize.height());
    painter.drawPixmap(target.toRect(), pm);
    painter.restore();
}

void ForestDashboardWidget::drawPond(QPainter& painter, const QPointF& center, qreal w, qreal h) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ECD995"));
    painter.drawEllipse(QRectF(center.x() - w * 0.62, center.y() - h * 0.72, w * 1.24, h * 1.44));
    painter.setBrush(QColor("#75C6DE"));
    painter.drawEllipse(QRectF(center.x() - w / 2, center.y() - h / 2, w, h));
    painter.setBrush(QColor("#AEE4ED"));
    painter.drawEllipse(QRectF(center.x() - w * 0.23, center.y() - h * 0.30, w * 0.40, h * 0.24));
    painter.setPen(QPen(QColor("#59AFC9"), 1, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(center.x() - w * 0.32, center.y() - h * 0.16, w * 0.64, h * 0.34), 195 * 16, 145 * 16);
    painter.restore();
}

void ForestDashboardWidget::drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                                        int flags, int minPointSize) const
{
    QFont font = painter.font();
    while (font.pointSize() > minPointSize) {
        QFontMetrics fm(font);
        if (fm.horizontalAdvance(text) <= rect.width()) break;
        font.setPointSize(font.pointSize() - 1);
    }
    painter.save();
    painter.setFont(font);
    painter.drawText(rect, flags, text);
    painter.restore();
}

