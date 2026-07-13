#include "core/FocusStatisticsQuery.h"
#include "config/PlantCatalog.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSet>

#include <algorithm>

namespace {

QDateTime localStart(const FocusRecord& record)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startTimestamp), Qt::LocalTime);
}

bool isCompletedOrAbandoned(const FocusRecord& record)
{
    return record.status == FocusRecordStatus::Success ||
           record.status == FocusRecordStatus::Abandoned;
}

qreal normalizedHash(uint32_t seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return static_cast<qreal>(seed % 10000) / 10000.0;
}

QString tagName(const QMap<uint32_t, QString>& names, uint32_t id)
{
    const QString value = names.value(id).trimmed();
    return value.isEmpty() ? QStringLiteral("无标签") : value;
}

}

FocusStatisticsQuery::Index::Index(const std::vector<FocusRecord>& records)
{
    rebuild(records);
}

void FocusStatisticsQuery::Index::rebuild(const std::vector<FocusRecord>& records)
{
    QElapsedTimer timer;
    timer.start();

    sourceRecordCount_ = static_cast<int>(records.size());
    entries_.clear();
    entries_.reserve(records.size());
    for (const FocusRecord& record : records) {
        if (!isCompletedOrAbandoned(record)) continue;
        entries_.push_back({&record, localStart(record).date()});
    }
    std::sort(entries_.begin(), entries_.end(), [](const Entry& lhs, const Entry& rhs) {
        if (lhs.date != rhs.date) return lhs.date < rhs.date;
        return lhs.record->startTimestamp < rhs.record->startTimestamp;
    });
    buildMicroseconds_ = timer.nsecsElapsed() / 1000;
}

int FocusStatisticsQuery::Index::sourceRecordCount() const
{
    return sourceRecordCount_;
}

qint64 FocusStatisticsQuery::Index::buildMicroseconds() const
{
    return buildMicroseconds_;
}

QPair<QDate, QDate> FocusStatisticsQuery::range(QDate selectedDate, Period period)
{
    switch (period) {
    case Period::Day:
        return {selectedDate, selectedDate};
    case Period::Week: {
        const QDate start = selectedDate.addDays(1 - selectedDate.dayOfWeek());
        return {start, start.addDays(6)};
    }
    case Period::Month: {
        const QDate start(selectedDate.year(), selectedDate.month(), 1);
        return {start, QDate(selectedDate.year(), selectedDate.month(), selectedDate.daysInMonth())};
    }
    case Period::Year:
        return {QDate(selectedDate.year(), 1, 1), QDate(selectedDate.year(), 12, 31)};
    }
    return {selectedDate, selectedDate};
}

QString FocusStatisticsQuery::periodLabel(QDate selectedDate, Period period)
{
    const auto dateRange = range(selectedDate, period);
    switch (period) {
    case Period::Day:
        return selectedDate.toString(QStringLiteral("yyyy年M月d日"));
    case Period::Week:
        return QStringLiteral("%1 - %2")
            .arg(dateRange.first.toString(QStringLiteral("M月d日")),
                 dateRange.second.toString(QStringLiteral("M月d日")));
    case Period::Month:
        return selectedDate.toString(QStringLiteral("yyyy年M月"));
    case Period::Year:
        return selectedDate.toString(QStringLiteral("yyyy年"));
    }
    return {};
}

FocusStatisticsQuery::Snapshot FocusStatisticsQuery::query(
    const std::vector<FocusRecord>& records,
    const QMap<uint32_t, QString>& tagNames,
    QDate selectedDate,
    Period period)
{
    const Index index(records);
    return query(index, tagNames, selectedDate, period);
}

FocusStatisticsQuery::Snapshot FocusStatisticsQuery::query(
    const Index& index,
    const QMap<uint32_t, QString>& tagNames,
    QDate selectedDate,
    Period period)
{
    QElapsedTimer timer;
    timer.start();

    Snapshot data;
    const auto dateRange = range(selectedDate, period);
    data.rangeText = periodLabel(selectedDate, period);
    data.metrics.sourceRecordCount = index.sourceRecordCount();
    data.metrics.indexBuildMicroseconds = index.buildMicroseconds();
    const auto finish = [&]() -> Snapshot {
        data.metrics.queryMicroseconds = timer.nsecsElapsed() / 1000;
        if (data.metrics.queryMicroseconds > 16000) {
            qInfo().nospace() << "[STATISTICS PERFORMANCE] records=" << data.metrics.sourceRecordCount
                              << " candidates=" << data.metrics.candidateRecordCount
                              << " query_us=" << data.metrics.queryMicroseconds
                              << " index_build_us=" << data.metrics.indexBuildMicroseconds;
        }
        return data;
    };

    switch (period) {
    case Period::Day:
        data.timeBuckets = QVector<int>(8, 0);
        data.axisLabels = {QStringLiteral("00:00"), QStringLiteral("06:00"), QStringLiteral("12:00"),
                           QStringLiteral("18:00"), QStringLiteral("23:00")};
        break;
    case Period::Week:
        data.timeBuckets = QVector<int>(7, 0);
        data.axisLabels = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
                           QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"),
                           QStringLiteral("周日")};
        break;
    case Period::Month:
        data.timeBuckets = QVector<int>(selectedDate.daysInMonth(), 0);
        data.axisLabels = {QStringLiteral("1日"),
                           QStringLiteral("%1日").arg((selectedDate.daysInMonth() + 1) / 2),
                           QStringLiteral("%1日").arg(selectedDate.daysInMonth())};
        break;
    case Period::Year:
        data.timeBuckets = QVector<int>(12, 0);
        data.axisLabels = {QStringLiteral("1月"), QStringLiteral("3月"), QStringLiteral("6月"),
                           QStringLiteral("9月"), QStringLiteral("12月")};
        break;
    }

    QSet<uint32_t> projectIds;
    QMap<QString, ProjectSlice> projectStats;
    QMap<uint32_t, int> treeCounts;

    const auto first = std::lower_bound(index.entries_.cbegin(), index.entries_.cend(), dateRange.first,
        [](const Index::Entry& entry, const QDate& date) { return entry.date < date; });
    const auto last = std::upper_bound(first, index.entries_.cend(), dateRange.second,
        [](const QDate& date, const Index::Entry& entry) { return date < entry.date; });
    data.metrics.candidateRecordCount = static_cast<int>(std::distance(first, last));

    for (auto it = first; it != last; ++it) {
        const FocusRecord& record = *it->record;
        const QDateTime started = localStart(record);
        const QDate date = started.date();

        const int minutes = std::max(1, static_cast<int>((record.actualSeconds + 59) / 60));
        ++data.focusCount;
        if (record.status == FocusRecordStatus::Success) {
            ++data.successCount;
            data.totalMinutes += minutes;
            data.growthValue += minutes * 2 + static_cast<int>(record.growthStage) * 6;
        } else {
            ++data.abandonedCount;
        }

        projectIds.insert(record.tagId);
        const int lastBucket = std::max(0, static_cast<int>(data.timeBuckets.size()) - 1);
        int bucket = 0;
        if (period == Period::Day) bucket = std::clamp(started.time().hour() / 3, 0, lastBucket);
        if (period == Period::Week) bucket = std::clamp(date.dayOfWeek() - 1, 0, lastBucket);
        if (period == Period::Month) bucket = std::clamp(date.day() - 1, 0, lastBucket);
        if (period == Period::Year) bucket = std::clamp(date.month() - 1, 0, lastBucket);
        if (record.status == FocusRecordStatus::Success) data.timeBuckets[bucket] += minutes;

        const QString project = tagName(tagNames, record.tagId);
        ProjectSlice& projectStat = projectStats[project];
        projectStat.name = project;
        ++projectStat.totalCount;
        if (record.status == FocusRecordStatus::Success) ++projectStat.successCount;
        else ++projectStat.abandonedCount;

        ++treeCounts[record.plantType];
        const QString timeText = period == Period::Day
            ? started.toString(QStringLiteral("HH:mm"))
            : started.toString(QStringLiteral("M月d日 HH:mm"));
        data.recentEvents.push_back({
            started.toSecsSinceEpoch(),
            timeText,
            project,
            record.status == FocusRecordStatus::Success ? QStringLiteral("成功") : QStringLiteral("放弃"),
            record.status == FocusRecordStatus::Success ? QColor("#4FAE8B") : QColor("#B98A5A")
        });

        const uint32_t seed = record.recordId * 2654435761u
            ^ static_cast<uint32_t>(record.startTimestamp)
            ^ (record.plantType + 17u) * 97u;
        data.islandPlants.push_back({
            record.recordId,
            record.plantType,
            record.status == FocusRecordStatus::Abandoned,
            0.16 + normalizedHash(seed) * 0.68,
            0.16 + normalizedHash(seed ^ 0x9E3779B9u) * 0.68,
            0.76 + normalizedHash(seed ^ 0x85EBCA6Bu) * 0.28,
            seed
        });
    }

    data.projectCount = projectIds.size();
    data.hasData = data.focusCount > 0;
    if (!data.hasData) {
        data.topTree = QStringLiteral("暂无");
        return finish();
    }

    const QColor colors[] = {QColor("#F4C85B"), QColor("#62BFA2"), QColor("#75B7D9"),
                             QColor("#A8C86B"), QColor("#E2A66B")};
    int colorIndex = 0;
    for (auto it = projectStats.cbegin(); it != projectStats.cend(); ++it) {
        ProjectSlice slice = it.value();
        slice.ratio = static_cast<double>(slice.totalCount) / data.focusCount;
        slice.color = colors[colorIndex++ % std::size(colors)];
        data.projects.push_back(slice);
    }
    std::sort(data.projects.begin(), data.projects.end(),
              [](const ProjectSlice& a, const ProjectSlice& b) { return a.ratio > b.ratio; });
    if (data.projects.size() > 4) {
        ProjectSlice other;
        other.name = QStringLiteral("其他");
        other.color = QColor("#A8C86B");
        for (int i = 3; i < data.projects.size(); ++i) {
            other.totalCount += data.projects[i].totalCount;
            other.successCount += data.projects[i].successCount;
            other.abandonedCount += data.projects[i].abandonedCount;
        }
        while (data.projects.size() > 3) data.projects.pop_back();
        other.ratio = static_cast<double>(other.totalCount) / data.focusCount;
        data.projects.push_back(other);
    }

    std::sort(data.recentEvents.begin(), data.recentEvents.end(),
              [](const SessionEvent& a, const SessionEvent& b) { return a.sortKey > b.sortKey; });
    while (data.recentEvents.size() > 4) data.recentEvents.pop_back();

    std::sort(data.islandPlants.begin(), data.islandPlants.end(),
              [](const IslandPlant& a, const IslandPlant& b) { return (a.u + a.v) < (b.u + b.v); });

    for (auto it = treeCounts.cbegin(); it != treeCounts.cend(); ++it) {
        const PlantDefinition& plant = PlantCatalog::byType(it.key());
        data.trees.push_back({plant.displayName, it.value(), plant.type});
    }
    std::sort(data.trees.begin(), data.trees.end(),
              [](const TreeRank& a, const TreeRank& b) { return a.count > b.count; });
    while (data.trees.size() > 3) data.trees.pop_back();

    data.topTree = data.trees.isEmpty() ? QStringLiteral("树苗") : data.trees.first().name;
    return finish();
}
