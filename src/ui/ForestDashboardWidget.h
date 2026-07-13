#ifndef FORESTDASHBOARDWIDGET_H
#define FORESTDASHBOARDWIDGET_H

#include <QWidget>
#include <QColor>
#include <QDate>
#include <QMap>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <limits>
#include <vector>

#include "common/DatabaseCommon.h"
#include "core/FocusStatisticsQuery.h"

class QPainter;
class QRectF;
class QMouseEvent;

// Desktop overview dashboard for "我的森林".
class ForestDashboardWidget : public QWidget {
    Q_OBJECT

public:
    struct RecordFilter {
        QDate date;
        uint32_t tagId = std::numeric_limits<uint32_t>::max();
        uint32_t plantType = std::numeric_limits<uint32_t>::max();
        int terminalState = -1; // -1 all, 0 success, 1 abandoned

        bool isActive() const {
            return date.isValid() || tagId != std::numeric_limits<uint32_t>::max() ||
                   plantType != std::numeric_limits<uint32_t>::max() || terminalState >= 0;
        }
    };

    explicit ForestDashboardWidget(QWidget* parent = nullptr);

    void setRecords(std::vector<FocusRecord> records);
    void setTagNames(const QMap<uint32_t, QString>& tagNames);
    void setRecordFilter(const RecordFilter& filter);
    RecordFilter recordFilter() const { return recordFilter_; }

signals:
    void settingsRequested();
    void focusRecordRequested(uint32_t recordId);
    void overviewRequested();
    void filtersRequested();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    enum class PeriodMode {
        Day,
        Week,
        Month,
        Year
    };

    using ProjectSlice = FocusStatisticsQuery::ProjectSlice;
    using TreeRank = FocusStatisticsQuery::TreeRank;
    using SessionEvent = FocusStatisticsQuery::SessionEvent;
    using IslandPlant = FocusStatisticsQuery::IslandPlant;
    using DashboardData = FocusStatisticsQuery::Snapshot;

    const DashboardData& buildData() const;

    void drawHeader(QPainter& painter, const QRectF& rect) const;
    void drawIslandPanel(QPainter& painter, const QRectF& rect, const DashboardData& data) const;
    void drawOverviewCards(QPainter& painter, const QRectF& rect, const DashboardData& data) const;
    void drawTimeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const;
    void drawProjectCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const;
    void drawTreeCard(QPainter& painter, const QRectF& rect, const DashboardData& data) const;

    void drawCard(QPainter& painter, const QRectF& rect, qreal radius = 22.0) const;
    void drawTree(QPainter& painter, QPointF base, qreal scale, const QColor& crown) const;
    void drawWitheredTree(QPainter& painter, QPointF base, qreal scale) const;
    void drawPlantImage(QPainter& painter, QPointF base, qreal scale,
                        uint32_t plantType, bool abandoned) const;
    void drawPond(QPainter& painter, const QPointF& center, qreal w, qreal h) const;
    void drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                     int flags, int minPointSize = 8) const;

    std::vector<FocusRecord> records_;
    std::vector<FocusRecord> allRecords_;
    FocusStatisticsQuery::Index statisticsIndex_;
    QMap<uint32_t, QString> tagNames_;
    QDate selectedDate_;
    PeriodMode periodMode_ = PeriodMode::Day;
    mutable QVector<QRectF> periodTabRects_;
    mutable QRectF prevDateRect_;
    mutable QRectF nextDateRect_;
    mutable QRectF settingsRect_;
    mutable QRectF overviewRect_;
    mutable QRectF filtersRect_;
    mutable QVector<QPair<QRectF, uint32_t>> islandPlantHitRects_;
    mutable std::optional<DashboardData> cachedData_;
    RecordFilter recordFilter_;
};

#endif // FORESTDASHBOARDWIDGET_H
