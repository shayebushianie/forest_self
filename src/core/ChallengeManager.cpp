#include "core/ChallengeManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

#include <algorithm>

namespace {

constexpr quint32 kChallengeMagic = 0x46434831u; // FCH1
constexpr quint16 kChallengeVersion = 1;
constexpr quint32 kMaxChallengeCount = 100000;

bool decodeChallenges(const QByteArray& bytes, QVector<Challenge>& challenges)
{
    if (bytes.isEmpty()) {
        challenges.clear();
        return true;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count > kMaxChallengeCount) return false;

    QVector<Challenge> loaded;
    loaded.reserve(static_cast<int>(count));
    for (quint32 i = 0; i < count; ++i) {
        Challenge challenge;
        qint64 startDate = 0;
        qint64 endDate = 0;
        uint8_t status = 0;
        stream >> challenge.id >> challenge.creatorId >> challenge.targetId >> challenge.targetMinutes
               >> startDate >> endDate >> status;
        if (stream.status() != QDataStream::Ok) return false;
        challenge.startDate = QDate::fromJulianDay(startDate);
        challenge.endDate = QDate::fromJulianDay(endDate);
        challenge.status = static_cast<ChallengeStatus>(status);
        challenge.creatorProgress = 0;
        challenge.targetProgress = 0;
        loaded.append(challenge);
    }
    if (!buffer.atEnd()) return false;
    challenges = std::move(loaded);
    return true;
}

QByteArray encodeChallenges(const QVector<Challenge>& challenges)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<quint32>(challenges.size());
    for (const Challenge& challenge : challenges) {
        stream << challenge.id << challenge.creatorId << challenge.targetId << challenge.targetMinutes
               << static_cast<qint64>(challenge.startDate.toJulianDay())
               << static_cast<qint64>(challenge.endDate.toJulianDay())
               << static_cast<uint8_t>(challenge.status);
    }
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

} // namespace

ChallengeManager::ChallengeManager(const QString& filePath, DatabaseManager* db,
                                   uint32_t localUserId)
    : m_filePath(filePath), m_db(db), m_localUserId(localUserId) {}

bool ChallengeManager::load()
{
    const auto result = StorageEnvelope::read(m_filePath, kChallengeMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) {
        m_challenges.clear();
        return true;
    }
    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_filePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kChallengeVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!decodeChallenges(bytes, m_challenges)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? save() : true;
}

bool ChallengeManager::save()
{
    return StorageEnvelope::write(m_filePath, kChallengeMagic, kChallengeVersion,
                                  encodeChallenges(m_challenges));
}

uint32_t ChallengeManager::nextId() const
{
    uint32_t id = 1;
    for (const Challenge& challenge : m_challenges)
        if (challenge.id >= id) id = challenge.id + 1;
    return id;
}

uint32_t ChallengeManager::focusMinutesBetween(uint32_t userId, const QDate& from,
                                               const QDate& to) const
{
    if (userId != m_localUserId || !m_db) return 0;
    uint32_t totalSeconds = 0;
    const auto records = m_db->getAllRecords();
    for (const FocusRecord& record : records) {
        if (record.status != FocusRecordStatus::Success) continue;
        const QDate date = QDateTime::fromSecsSinceEpoch(record.startTimestamp).date();
        if (date >= from && date <= to) totalSeconds += record.actualSeconds;
    }
    return totalSeconds / 60;
}

bool ChallengeManager::createChallenge(uint32_t targetUserId, uint32_t targetMinutes,
                                       const QDate& endDate)
{
    Challenge challenge;
    challenge.id = nextId();
    challenge.creatorId = m_localUserId;
    challenge.targetId = targetUserId;
    challenge.targetMinutes = targetMinutes;
    challenge.startDate = QDate::currentDate();
    challenge.endDate = endDate;
    challenge.status = ChallengeStatus::PENDING;
    challenge.creatorProgress = 0;
    challenge.targetProgress = 0;
    m_challenges.append(challenge);
    if (save()) return true;
    m_challenges.removeLast();
    return false;
}

bool ChallengeManager::acceptChallenge(uint32_t challengeId)
{
    for (Challenge& challenge : m_challenges) {
        if (challenge.id != challengeId || challenge.targetId != m_localUserId ||
            challenge.status != ChallengeStatus::PENDING) continue;
        const ChallengeStatus previous = challenge.status;
        challenge.status = ChallengeStatus::ACCEPTED;
        if (save()) return true;
        challenge.status = previous;
        return false;
    }
    return false;
}

bool ChallengeManager::rejectChallenge(uint32_t challengeId)
{
    for (Challenge& challenge : m_challenges) {
        if (challenge.id != challengeId || challenge.targetId != m_localUserId ||
            challenge.status != ChallengeStatus::PENDING) continue;
        const ChallengeStatus previous = challenge.status;
        challenge.status = ChallengeStatus::REJECTED;
        if (save()) return true;
        challenge.status = previous;
        return false;
    }
    return false;
}

QVector<Challenge> ChallengeManager::incoming() const
{
    QVector<Challenge> result;
    for (const Challenge& challenge : m_challenges)
        if (challenge.targetId == m_localUserId && challenge.status == ChallengeStatus::PENDING)
            result.append(challenge);
    return result;
}

QVector<Challenge> ChallengeManager::outgoing() const
{
    QVector<Challenge> result;
    for (const Challenge& challenge : m_challenges)
        if (challenge.creatorId == m_localUserId) result.append(challenge);
    return result;
}

QVector<Challenge> ChallengeManager::active() const
{
    QVector<Challenge> result;
    for (const Challenge& challenge : m_challenges) {
        if (challenge.status == ChallengeStatus::PENDING || challenge.status == ChallengeStatus::ACCEPTED) {
            Challenge copy = challenge;
            recalculateProgress(copy);
            result.append(copy);
        }
    }
    return result;
}

QVector<Challenge> ChallengeManager::history() const
{
    QVector<Challenge> result;
    for (const Challenge& challenge : m_challenges)
        if (challenge.status == ChallengeStatus::COMPLETED ||
            challenge.status == ChallengeStatus::REJECTED ||
            challenge.status == ChallengeStatus::EXPIRED)
            result.append(challenge);
    return result;
}

void ChallengeManager::recalculateProgress(Challenge& challenge) const
{
    const QDate end = challenge.endDate;
    const QDate to = std::min(QDate::currentDate(), end);
    challenge.creatorProgress = focusMinutesBetween(challenge.creatorId, challenge.startDate, to);
    challenge.targetProgress = focusMinutesBetween(challenge.targetId, challenge.startDate, to);
}

bool ChallengeManager::refreshProgressAfterFocus()
{
    const QVector<Challenge> previous = m_challenges;
    bool changed = false;
    const QDate today = QDate::currentDate();
    for (Challenge& challenge : m_challenges) {
        if (challenge.status != ChallengeStatus::PENDING &&
            challenge.status != ChallengeStatus::ACCEPTED) continue;

        if (today > challenge.endDate) {
            challenge.status = ChallengeStatus::EXPIRED;
            changed = true;
            continue;
        }
        if (challenge.status == ChallengeStatus::ACCEPTED &&
            focusMinutesBetween(m_localUserId, challenge.startDate, today) >= challenge.targetMinutes) {
            challenge.status = ChallengeStatus::COMPLETED;
            changed = true;
        }
    }
    if (!changed || save()) return true;
    m_challenges = previous;
    return false;
}
