#include "core/GuardianManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

#include <algorithm>

namespace {

constexpr quint32 kGuardianMagic = 0x46474431u; // FGD1
constexpr quint16 kGuardianVersion = 2;

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

QByteArray encodeState(uint32_t dailyGoal, uint32_t todayMinutes, uint32_t currentStreak,
                       uint32_t longestStreak, uint32_t totalMinutes, const QDate& lastActiveDate,
                       const QSet<uint32_t>& appliedFocusRecords)
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
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

}

GuardianManager::GuardianManager(const QString& filePath) : m_filePath(filePath) {}

bool GuardianManager::load()
{
    const auto result = StorageEnvelope::read(m_filePath, kGuardianMagic);
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

    const bool migrate = result.state == StorageEnvelope::ReadState::Legacy || result.version == 1;
    if (result.version == kGuardianVersion && result.hasPayload()) {
        if (!decodeStateV2(bytes, m_dailyGoal, m_todayMinutes, m_currentStreak,
                           m_longestStreak, m_totalMinutes, m_lastActiveDate,
                           m_appliedFocusRecords)) return false;
    } else if (!decodeStateV1(bytes, m_dailyGoal, m_todayMinutes, m_currentStreak,
                              m_longestStreak, m_totalMinutes, m_lastActiveDate)) {
        return false;
    }
    if (migrate) m_appliedFocusRecords.clear();
    return migrate ? save() : true;
}

bool GuardianManager::save()
{
    return StorageEnvelope::write(m_filePath, kGuardianMagic, kGuardianVersion,
                                  encodeState(m_dailyGoal, m_todayMinutes, m_currentStreak,
                                              m_longestStreak, m_totalMinutes, m_lastActiveDate,
                                              m_appliedFocusRecords));
}

bool GuardianManager::addCompletedFocusMinutes(uint32_t recordId, uint32_t minutes)
{
    if (m_appliedFocusRecords.contains(recordId)) return true;
    const uint32_t previousTodayMinutes = m_todayMinutes;
    const uint32_t previousCurrentStreak = m_currentStreak;
    const uint32_t previousLongestStreak = m_longestStreak;
    const uint32_t previousTotalMinutes = m_totalMinutes;
    const QDate previousLastActiveDate = m_lastActiveDate;

    checkDayBoundary();
    m_todayMinutes += minutes;
    m_totalMinutes += minutes;
    if (m_todayMinutes >= m_dailyGoal) m_currentStreak = 1;
    m_appliedFocusRecords.insert(recordId);
    if (save()) return true;

    m_todayMinutes = previousTodayMinutes;
    m_currentStreak = previousCurrentStreak;
    m_longestStreak = previousLongestStreak;
    m_totalMinutes = previousTotalMinutes;
    m_lastActiveDate = previousLastActiveDate;
    m_appliedFocusRecords.remove(recordId);
    return false;
}

void GuardianManager::checkDayBoundary()
{
    const QDate today = QDate::currentDate();
    if (!m_lastActiveDate.isValid()) {
        m_lastActiveDate = today;
        m_todayMinutes = 0;
        return;
    }
    if (today == m_lastActiveDate) return;

    const int daysPassed = m_lastActiveDate.daysTo(today);
    if (daysPassed == 1) {
        if (m_todayMinutes >= m_dailyGoal) {
            ++m_currentStreak;
            if (m_currentStreak > m_longestStreak) m_longestStreak = m_currentStreak;
        } else {
            m_currentStreak = 0;
        }
    } else {
        m_currentStreak = 0;
    }
    m_todayMinutes = 0;
    m_lastActiveDate = today;
}
