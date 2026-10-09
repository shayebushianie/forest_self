#include "core/GuardianManager.h"
#include "storage/StorageEnvelope.h"
#include "core/FocusRecordSemantics.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

#include <algorithm>

namespace {

constexpr quint32 kGuardianMagic = 0x46474431u; // FGD1
constexpr quint16 kGuardianVersion = 3;
using DailyProgress = QMap<QDate, QPair<quint64, quint32>>;

bool decodeStateV1(const QByteArray& bytes, uint32_t& dailyGoal, uint32_t& todayMinutes,
                   uint32_t& currentStreak, uint32_t& longestStreak,
                   uint32_t& totalMinutes, QDate& lastActiveDate)
{
    if (bytes.isEmpty()) return true;
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    qint64 julianDay = 0;
    stream >> dailyGoal >> todayMinutes >> currentStreak >> longestStreak >> totalMinutes >> julianDay;
    if (stream.status() != QDataStream::Ok || !buffer.atEnd()) return false;
    lastActiveDate = QDate::fromJulianDay(julianDay);
    return true;
}

bool decodeStateV2(const QByteArray& bytes, uint32_t& dailyGoal, uint32_t& todayMinutes,
                   uint32_t& currentStreak, uint32_t& longestStreak,
                   uint32_t& totalMinutes, QDate& lastActiveDate,
                   QSet<uint32_t>& appliedFocusRecords)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    qint64 julianDay = 0;
    quint32 count = 0;
    stream >> dailyGoal >> todayMinutes >> currentStreak >> longestStreak >> totalMinutes >> julianDay >> count;
    if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
    QSet<uint32_t> loaded;
    for (quint32 i = 0; i < count; ++i) {
        uint32_t recordId = 0;
        stream >> recordId;
        if (stream.status() != QDataStream::Ok) return false;
        loaded.insert(recordId);
    }
    if (!buffer.atEnd()) return false;
    lastActiveDate = QDate::fromJulianDay(julianDay);
    appliedFocusRecords = std::move(loaded);
    return true;
}

bool decodeStateV3(const QByteArray& bytes, uint32_t& dailyGoal, uint32_t& todayMinutes,
                   uint32_t& currentStreak, uint32_t& longestStreak,
                   uint32_t& totalMinutes, QDate& lastActiveDate,
                   QSet<uint32_t>& appliedFocusRecords, quint64& totalSeconds,
                   DailyProgress& dailyProgress, bool& historyReconciled)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    qint64 julianDay = 0;
    quint32 receiptCount = 0;
    quint32 dayCount = 0;
    stream >> dailyGoal >> todayMinutes >> currentStreak >> longestStreak >> totalMinutes
           >> julianDay >> receiptCount;
    if (stream.status() != QDataStream::Ok || receiptCount > 1000000u) return false;

    QSet<uint32_t> loadedReceipts;
    for (quint32 i = 0; i < receiptCount; ++i) {
        uint32_t recordId = 0;
        stream >> recordId;
        if (stream.status() != QDataStream::Ok) return false;
        loadedReceipts.insert(recordId);
    }
    stream >> totalSeconds >> dayCount;
    if (stream.status() != QDataStream::Ok || dayCount > 1000000u) return false;

    DailyProgress loadedDays;
    for (quint32 i = 0; i < dayCount; ++i) {
        qint64 dayNumber = 0;
        quint64 seconds = 0;
        quint32 goalMinutes = 0;
        stream >> dayNumber >> seconds >> goalMinutes;
        const QDate date = QDate::fromJulianDay(dayNumber);
        if (stream.status() != QDataStream::Ok || !date.isValid() || loadedDays.contains(date))
            return false;
        loadedDays.insert(date, qMakePair(seconds, goalMinutes));
    }
    stream >> historyReconciled;
    if (stream.status() != QDataStream::Ok || !buffer.atEnd()) return false;

    lastActiveDate = QDate::fromJulianDay(julianDay);
    appliedFocusRecords = std::move(loadedReceipts);
    dailyProgress = std::move(loadedDays);
    return true;
}

QByteArray encodeState(uint32_t dailyGoal, uint32_t todayMinutes, uint32_t currentStreak,
                       uint32_t longestStreak, uint32_t totalMinutes, const QDate& lastActiveDate,
                       const QSet<uint32_t>& appliedFocusRecords, quint64 totalSeconds,
                       const DailyProgress& dailyProgress, bool historyReconciled)
{
    QList<uint32_t> recordIds = appliedFocusRecords.values();
    std::sort(recordIds.begin(), recordIds.end());
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << dailyGoal << todayMinutes << currentStreak << longestStreak << totalMinutes
           << static_cast<qint64>(lastActiveDate.toJulianDay())
           << static_cast<quint32>(recordIds.size());
    for (uint32_t recordId : recordIds) stream << recordId;
    stream << totalSeconds << static_cast<quint32>(dailyProgress.size());
    for (auto it = dailyProgress.cbegin(); it != dailyProgress.cend(); ++it) {
        stream << static_cast<qint64>(it.key().toJulianDay()) << it.value().first << it.value().second;
    }
    stream << historyReconciled;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

}

GuardianManager::GuardianManager(const QString& filePath) : m_filePath(filePath) {}

bool GuardianManager::load()
{
    m_lastError.clear();
    const auto result = StorageEnvelope::inspect(m_filePath, kGuardianMagic);
    if (result.state == StorageEnvelope::ReadState::Corrupt ||
        (result.state == StorageEnvelope::ReadState::Legacy &&
         StorageEnvelope::inspect(m_filePath + ".bak", kGuardianMagic).hasPayload()) ||
        (result.state == StorageEnvelope::ReadState::Missing && QFile::exists(m_filePath + ".bak"))) {
        m_lastError = QStringLiteral("守护主文件损坏或缺失，单独回退可能与专注结算不一致。请恢复完整数据快照；原文件与 .bak 已保留。");
        return false;
    }
    if (result.state == StorageEnvelope::ReadState::Missing) return true;

    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
    } else if (result.hasPayload()) {
        bytes = result.payload;
    } else {
        return false;
    }

    if (result.hasPayload() && (result.version < 1 || result.version > kGuardianVersion)) return false;
    const bool migrate = result.state == StorageEnvelope::ReadState::Legacy || result.version < kGuardianVersion;
    if (result.version == kGuardianVersion && result.hasPayload()) {
        if (!decodeStateV3(bytes, m_dailyGoal, m_todayMinutes, m_currentStreak,
                           m_longestStreak, m_totalMinutes, m_lastActiveDate,
                           m_appliedFocusRecords, m_totalSeconds, m_dailyProgress,
                           m_historyReconciled)) return false;
    } else if (result.version == 2 && result.hasPayload()) {
        if (!decodeStateV2(bytes, m_dailyGoal, m_todayMinutes, m_currentStreak,
                           m_longestStreak, m_totalMinutes, m_lastActiveDate,
                           m_appliedFocusRecords)) return false;
    } else if (!decodeStateV1(bytes, m_dailyGoal, m_todayMinutes, m_currentStreak,
                              m_longestStreak, m_totalMinutes, m_lastActiveDate)) {
        return false;
    }
    if (migrate) {
        m_historyReconciled = false;
        if (result.version < 2) m_appliedFocusRecords.clear();
        m_totalSeconds = static_cast<quint64>(m_totalMinutes) * 60;
        m_dailyProgress.clear();
        if (m_lastActiveDate.isValid()) {
            m_dailyProgress.insert(m_lastActiveDate,
                                   qMakePair(static_cast<quint64>(m_todayMinutes) * 60, m_dailyGoal));
        }
    }
    return migrate ? save() : true;
}

bool GuardianManager::save()
{
    m_lastError.clear();
    if (!StorageEnvelope::write(m_filePath, kGuardianMagic, kGuardianVersion,
                                encodeState(m_dailyGoal, m_todayMinutes, m_currentStreak,
                                            m_longestStreak, m_totalMinutes, m_lastActiveDate,
                                            m_appliedFocusRecords, m_totalSeconds, m_dailyProgress,
                                            m_historyReconciled),
                                &m_lastError)) return false;
    return true;
}

bool GuardianManager::setDailyGoalMinutes(uint32_t minutes)
{
    if (minutes == 0) return false;
    if (minutes == m_dailyGoal) return true;
    const uint32_t previousGoal = m_dailyGoal;
    const auto previousDays = m_dailyProgress;
    const auto previousCurrent = m_currentStreak;
    const auto previousLongest = m_longestStreak;
    const auto previousDate = m_lastActiveDate;
    const auto previousToday = m_todayMinutes;
    m_dailyGoal = minutes;
    const QDate today = QDate::currentDate();
    auto day = m_dailyProgress.value(today, qMakePair(quint64(0), minutes));
    day.second = minutes;
    m_dailyProgress.insert(today, day);
    refreshSummary(today);
    if (save()) return true;
    m_dailyGoal = previousGoal;
    m_dailyProgress = previousDays;
    m_currentStreak = previousCurrent;
    m_longestStreak = previousLongest;
    m_lastActiveDate = previousDate;
    m_todayMinutes = previousToday;
    return false;
}

uint32_t GuardianManager::minutesForDate(const QDate& date) const
{
    const auto it = m_dailyProgress.constFind(date);
    return it == m_dailyProgress.cend()
        ? 0u : static_cast<uint32_t>(FocusRecordSemantics::wholeMinutes(it.value().first));
}

bool GuardianManager::dateReachedGoal(const QDate& date) const
{
    const auto it = m_dailyProgress.constFind(date);
    return it != m_dailyProgress.cend() && it.value().second > 0 &&
           it.value().first >= static_cast<quint64>(it.value().second) * 60;
}

uint32_t GuardianManager::streakForDate(const QDate& date) const
{
    QDate cursor = dateReachedGoal(date) ? date : date.addDays(-1);
    uint32_t streak = 0;
    while (cursor.isValid() && dateReachedGoal(cursor)) {
        ++streak;
        cursor = cursor.addDays(-1);
    }
    return streak;
}

uint32_t GuardianManager::computedLongestStreak() const
{
    uint32_t longest = 0;
    uint32_t streak = 0;
    QDate previousDate;
    for (auto it = m_dailyProgress.cbegin(); it != m_dailyProgress.cend(); ++it) {
        if (!dateReachedGoal(it.key())) {
            streak = 0;
            previousDate = {};
            continue;
        }
        streak = previousDate.isValid() && previousDate.daysTo(it.key()) == 1 ? streak + 1 : 1;
        longest = std::max(longest, streak);
        previousDate = it.key();
    }
    return longest;
}

void GuardianManager::refreshSummary(const QDate& today)
{
    if (!m_dailyProgress.contains(today))
        m_dailyProgress.insert(today, qMakePair(quint64(0), m_dailyGoal));
    m_lastActiveDate = today;
    m_todayMinutes = minutesForDate(today);
    m_currentStreak = streakForDate(today);
    m_longestStreak = std::max(m_longestStreak, computedLongestStreak());
}

uint32_t GuardianManager::todayMinutes() const
{
    return minutesForDate(QDate::currentDate());
}

uint32_t GuardianManager::currentStreak() const
{
    return streakForDate(QDate::currentDate());
}

bool GuardianManager::addCompletedFocusMinutes(uint32_t recordId, uint32_t minutes)
{
    return addCompletedFocusSeconds(recordId, static_cast<quint64>(minutes) * 60,
                                    QDate::currentDate());
}

bool GuardianManager::addCompletedFocusSeconds(uint32_t recordId, uint64_t seconds,
                                               const QDate& startDate)
{
    if (m_appliedFocusRecords.contains(recordId)) return true;
    if (!startDate.isValid()) {
        m_lastError = QStringLiteral("专注记录开始日期无效，无法更新时间守护。");
        return false;
    }

    const uint32_t previousTodayMinutes = m_todayMinutes;
    const uint32_t previousCurrentStreak = m_currentStreak;
    const uint32_t previousLongestStreak = m_longestStreak;
    const uint32_t previousTotalMinutes = m_totalMinutes;
    const quint64 previousTotalSeconds = m_totalSeconds;
    const QDate previousLastActiveDate = m_lastActiveDate;
    const DailyProgress previousDailyProgress = m_dailyProgress;
    const QSet<uint32_t> previousAppliedRecords = m_appliedFocusRecords;

    auto day = m_dailyProgress.value(startDate, qMakePair(quint64(0), m_dailyGoal));
    day.first += seconds;
    m_dailyProgress.insert(startDate, day);
    m_totalSeconds += seconds;
    m_totalMinutes = std::max(m_totalMinutes,
        static_cast<uint32_t>(FocusRecordSemantics::wholeMinutes(m_totalSeconds)));
    m_appliedFocusRecords.insert(recordId);
    refreshSummary(QDate::currentDate());
    if (save()) return true;

    m_todayMinutes = previousTodayMinutes;
    m_currentStreak = previousCurrentStreak;
    m_longestStreak = previousLongestStreak;
    m_totalMinutes = previousTotalMinutes;
    m_totalSeconds = previousTotalSeconds;
    m_lastActiveDate = previousLastActiveDate;
    m_dailyProgress = previousDailyProgress;
    m_appliedFocusRecords = previousAppliedRecords;
    return false;
}

bool GuardianManager::reconcileCompletedFocusRecords(const std::vector<FocusRecord>& records)
{
    if (m_historyReconciled) return true;
    QMap<QDate, quint64> secondsByDate;
    QSet<uint32_t> recordIds;
    quint64 recordsTotalSeconds = 0;
    for (const FocusRecord& record : records) {
        if (record.status != FocusRecordStatus::Success) continue;
        const QDate date = FocusRecordSemantics::startDate(record);
        if (!date.isValid()) {
            m_lastError = QStringLiteral("专注记录开始日期无效，无法重算时间守护。");
            return false;
        }
        secondsByDate[date] += record.actualSeconds;
        recordsTotalSeconds += record.actualSeconds;
        recordIds.insert(record.recordId);
    }

    const uint32_t previousTodayMinutes = m_todayMinutes;
    const uint32_t previousCurrentStreak = m_currentStreak;
    const uint32_t previousLongestStreak = m_longestStreak;
    const uint32_t previousTotalMinutes = m_totalMinutes;
    const quint64 previousTotalSeconds = m_totalSeconds;
    const QDate previousLastActiveDate = m_lastActiveDate;
    const DailyProgress previousDailyProgress = m_dailyProgress;
    const QSet<uint32_t> previousAppliedRecords = m_appliedFocusRecords;

    const bool previousReconciled = m_historyReconciled;
    m_dailyProgress.clear();
    for (auto it = secondsByDate.cbegin(); it != secondsByDate.cend(); ++it) {
        auto day = previousDailyProgress.value(it.key(), qMakePair(quint64(0), m_dailyGoal));
        day.first = it.value();
        m_dailyProgress.insert(it.key(), day);
    }
    m_totalSeconds = std::max(m_totalSeconds, recordsTotalSeconds);
    m_totalMinutes = std::max(m_totalMinutes,
        static_cast<uint32_t>(FocusRecordSemantics::wholeMinutes(m_totalSeconds)));
    m_appliedFocusRecords.unite(recordIds);
    m_historyReconciled = true;
    refreshSummary(QDate::currentDate());
    if (save()) return true;

    m_todayMinutes = previousTodayMinutes;
    m_currentStreak = previousCurrentStreak;
    m_longestStreak = previousLongestStreak;
    m_totalMinutes = previousTotalMinutes;
    m_totalSeconds = previousTotalSeconds;
    m_lastActiveDate = previousLastActiveDate;
    m_dailyProgress = previousDailyProgress;
    m_appliedFocusRecords = previousAppliedRecords;
    m_historyReconciled = previousReconciled;
    return false;
}

bool GuardianManager::checkDayBoundary()
{
    return checkDayBoundary(QDate::currentDate());
}

bool GuardianManager::checkDayBoundary(const QDate& today)
{
    if (!today.isValid()) return false;
    if (m_lastActiveDate.isValid() && today <= m_lastActiveDate) return true;

    const uint32_t previousTodayMinutes = m_todayMinutes;
    const uint32_t previousCurrentStreak = m_currentStreak;
    const uint32_t previousLongestStreak = m_longestStreak;
    const QDate previousLastActiveDate = m_lastActiveDate;
    const DailyProgress previousDailyProgress = m_dailyProgress;
    refreshSummary(today);
    if (save()) return true;

    m_todayMinutes = previousTodayMinutes;
    m_currentStreak = previousCurrentStreak;
    m_longestStreak = previousLongestStreak;
    m_lastActiveDate = previousLastActiveDate;
    m_dailyProgress = previousDailyProgress;
    return false;
}
