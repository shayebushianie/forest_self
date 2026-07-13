#include "core/FocusStatisticsQuery.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMap>

#include <iostream>
#include <vector>

namespace {

bool expect(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << "\n";
    return value;
}

FocusRecord record(uint32_t id, const QDate& date, int hour, FocusRecordStatus status, uint32_t tagId)
{
    FocusRecord value;
    value.recordId = id;
    value.status = status;
    value.tagId = tagId;
    value.actualSeconds = 30 * 60;
    value.startTimestamp = static_cast<uint64_t>(QDateTime(date, QTime(hour, 0), Qt::LocalTime).toSecsSinceEpoch());
    return value;
}

bool testPeriodsAndProjects()
{
    const QDate selected(2024, 3, 1); // Friday in a leap year.
    std::vector<FocusRecord> records = {
        record(0, selected, 9, FocusRecordStatus::Success, 1),
        record(1, selected, 12, FocusRecordStatus::Abandoned, 1),
        record(2, selected.addDays(-4), 10, FocusRecordStatus::Success, 2),
        record(3, selected.addDays(31), 11, FocusRecordStatus::Success, 1)
    };
    const QMap<uint32_t, QString> tags = {{1, QStringLiteral("学习")}, {2, QStringLiteral("阅读")}};
    const auto day = FocusStatisticsQuery::query(records, tags, selected, FocusStatisticsQuery::Period::Day);
    const auto week = FocusStatisticsQuery::query(records, tags, selected, FocusStatisticsQuery::Period::Week);
    const auto month = FocusStatisticsQuery::query(records, tags, selected, FocusStatisticsQuery::Period::Month);
    const auto year = FocusStatisticsQuery::query(records, tags, selected, FocusStatisticsQuery::Period::Year);
    return expect(day.focusCount == 2 && day.successCount == 1 && day.abandonedCount == 1,
                  "day statistics preserve success and abandonment") &&
           expect(day.projects.size() == 1 && day.projects.first().totalCount == 2 &&
                  day.projects.first().successCount == 1 && day.projects.first().abandonedCount == 1,
                  "project statistics count sessions rather than duration") &&
           expect(week.timeBuckets.size() == 7 && week.focusCount == 3,
                  "week starts on Monday and includes local dates") &&
           expect(month.timeBuckets.size() == 31 && month.focusCount == 2,
                  "month uses the selected calendar month") &&
           expect(year.timeBuckets.size() == 12 && year.focusCount == 4,
                  "year aggregates calendar months");
}

bool sameSnapshot(const FocusStatisticsQuery::Snapshot& lhs,
                  const FocusStatisticsQuery::Snapshot& rhs)
{
    if (lhs.totalMinutes != rhs.totalMinutes || lhs.focusCount != rhs.focusCount ||
        lhs.successCount != rhs.successCount || lhs.abandonedCount != rhs.abandonedCount ||
        lhs.projectCount != rhs.projectCount || lhs.growthValue != rhs.growthValue ||
        lhs.topTree != rhs.topTree || lhs.timeBuckets != rhs.timeBuckets ||
        lhs.axisLabels != rhs.axisLabels || lhs.projects.size() != rhs.projects.size() ||
        lhs.trees.size() != rhs.trees.size() || lhs.recentEvents.size() != rhs.recentEvents.size() ||
        lhs.islandPlants.size() != rhs.islandPlants.size()) return false;

    for (int i = 0; i < lhs.projects.size(); ++i) {
        const auto& a = lhs.projects[i];
        const auto& b = rhs.projects[i];
        if (a.name != b.name || a.ratio != b.ratio || a.color != b.color ||
            a.totalCount != b.totalCount || a.successCount != b.successCount ||
            a.abandonedCount != b.abandonedCount) return false;
    }
    for (int i = 0; i < lhs.trees.size(); ++i) {
        if (lhs.trees[i].name != rhs.trees[i].name || lhs.trees[i].count != rhs.trees[i].count ||
            lhs.trees[i].plantType != rhs.trees[i].plantType) return false;
    }
    for (int i = 0; i < lhs.recentEvents.size(); ++i) {
        const auto& a = lhs.recentEvents[i];
        const auto& b = rhs.recentEvents[i];
        if (a.sortKey != b.sortKey || a.timeText != b.timeText || a.projectName != b.projectName ||
            a.statusText != b.statusText || a.color != b.color) return false;
    }
    for (int i = 0; i < lhs.islandPlants.size(); ++i) {
        const auto& a = lhs.islandPlants[i];
        const auto& b = rhs.islandPlants[i];
        if (a.plantType != b.plantType || a.abandoned != b.abandoned || a.u != b.u ||
            a.v != b.v || a.scale != b.scale || a.placementSeed != b.placementSeed) return false;
    }
    return true;
}

bool testLargeRecordIndexPerformance()
{
    constexpr int kRecordCount = 120000;
    const QDate firstDate(2018, 1, 1);
    std::vector<FocusRecord> records;
    records.reserve(kRecordCount);
    for (int i = 0; i < kRecordCount; ++i) {
        FocusRecord value;
        value.recordId = static_cast<uint32_t>(i);
        value.status = (i % 7 == 0) ? FocusRecordStatus::Running
            : ((i % 5 == 0) ? FocusRecordStatus::Abandoned : FocusRecordStatus::Success);
        value.plantType = static_cast<uint32_t>(i % 6);
        value.tagId = static_cast<uint32_t>(i % 4 + 1);
        value.actualSeconds = static_cast<uint32_t>((20 + i % 41) * 60);
        value.growthStage = static_cast<uint32_t>(i % 4);
        const QDate date = firstDate.addDays(i % 2922);
        value.startTimestamp = static_cast<uint64_t>(
            QDateTime(date, QTime((i / 60) % 24, i % 60), Qt::LocalTime).toSecsSinceEpoch());
        records.push_back(value);
    }

    const QMap<uint32_t, QString> tags = {
        {1, QStringLiteral("学习")}, {2, QStringLiteral("工作")},
        {3, QStringLiteral("阅读")}, {4, QStringLiteral("其他")}
    };
    const QDate selected(2024, 7, 10);
    FocusStatisticsQuery::Index index(records);
    if (!expect(index.sourceRecordCount() == kRecordCount,
                "statistics index records the raw FocusRecord count")) return false;

    qint64 totalIndexedMilliseconds = 0;
    const FocusStatisticsQuery::Period periods[] = {
        FocusStatisticsQuery::Period::Day, FocusStatisticsQuery::Period::Week,
        FocusStatisticsQuery::Period::Month, FocusStatisticsQuery::Period::Year
    };
    for (const auto period : periods) {
        QElapsedTimer timer;
        timer.start();
        const auto indexed = FocusStatisticsQuery::query(index, tags, selected, period);
        totalIndexedMilliseconds += timer.elapsed();
        const auto compatibility = FocusStatisticsQuery::query(records, tags, selected, period);
        if (!expect(sameSnapshot(indexed, compatibility),
                    "indexed statistics preserve the existing snapshot semantics")) return false;
        if (!expect(indexed.metrics.sourceRecordCount == kRecordCount &&
                    indexed.metrics.candidateRecordCount > 0 &&
                    indexed.metrics.candidateRecordCount < kRecordCount,
                    "statistics metrics expose source and date-range record counts")) return false;
        if (!expect(indexed.metrics.queryMicroseconds < 150000,
                    "each indexed period query completes within the interactive budget")) return false;
    }
    return expect(totalIndexedMilliseconds < 400,
                  "day week month year switching stays responsive for a large record history");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return testPeriodsAndProjects() && testLargeRecordIndexPerformance() ? 0 : 1;
}
