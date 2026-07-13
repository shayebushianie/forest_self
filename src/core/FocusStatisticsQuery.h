#ifndef FOCUS_STATISTICS_QUERY_H
#define FOCUS_STATISTICS_QUERY_H

#include <QColor>
#include <QDate>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <vector>

#include "common/DatabaseCommon.h"

class FocusStatisticsQuery final {
public:
    enum class Period { Day, Week, Month, Year };
    struct QueryMetrics {
        int sourceRecordCount = 0;
        int candidateRecordCount = 0;
        qint64 indexBuildMicroseconds = 0;
        qint64 queryMicroseconds = 0;
    };
    class Index final {
    public:
        Index() = default;
        explicit Index(const std::vector<FocusRecord>& records);

        void rebuild(const std::vector<FocusRecord>& records);
        int sourceRecordCount() const;
        qint64 buildMicroseconds() const;

    private:
        struct Entry {
            const FocusRecord* record = nullptr;
            QDate date;
        };

        std::vector<Entry> entries_;
        int sourceRecordCount_ = 0;
        qint64 buildMicroseconds_ = 0;

        friend class FocusStatisticsQuery;
    };
    struct ProjectSlice { QString name; double ratio = 0.0; QColor color; int totalCount = 0; int successCount = 0; int abandonedCount = 0; };
    struct TreeRank { QString name; int count = 0; uint32_t plantType = 0; };
    struct SessionEvent { qint64 sortKey = 0; QString timeText; QString projectName; QString statusText; QColor color; };
    struct IslandPlant { uint32_t recordId = 0; uint32_t plantType = 0; bool abandoned = false; qreal u = 0.5; qreal v = 0.5; qreal scale = 1.0; uint32_t placementSeed = 0; };
    struct Snapshot {
        int totalMinutes = 0;
        int focusCount = 0;
        int successCount = 0;
        int abandonedCount = 0;
        int projectCount = 0;
        int growthValue = 0;
        QString topTree;
        QVector<int> timeBuckets;
        QStringList axisLabels;
        QVector<ProjectSlice> projects;
        QVector<TreeRank> trees;
        QVector<SessionEvent> recentEvents;
        QVector<IslandPlant> islandPlants;
        bool hasData = false;
        QString rangeText;
        QueryMetrics metrics;
    };

    static Snapshot query(const std::vector<FocusRecord>& records,
                          const QMap<uint32_t, QString>& tagNames,
                          QDate selectedDate, Period period);
    static Snapshot query(const Index& index,
                          const QMap<uint32_t, QString>& tagNames,
                          QDate selectedDate, Period period);
    static QPair<QDate, QDate> range(QDate selectedDate, Period period);
    static QString periodLabel(QDate selectedDate, Period period);
};

#endif // FOCUS_STATISTICS_QUERY_H
