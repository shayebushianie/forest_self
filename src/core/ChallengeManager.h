#ifndef CHALLENGEMANAGER_H
#define CHALLENGEMANAGER_H

#include <QString>
#include <QVector>
#include <QDate>
#include <cstdint>
#include "storage/DatabaseManager.h"

enum class ChallengeStatus : uint8_t {
    PENDING = 0,
    ACCEPTED = 1,
    COMPLETED = 2,
    REJECTED = 3,
    EXPIRED = 4
};

struct Challenge {
    uint32_t id;
    uint32_t creatorId;
    uint32_t targetId;
    uint32_t targetMinutes;   // total focus minutes to reach
    QDate    startDate;       // challenge start
    QDate    endDate;         // challenge deadline
    ChallengeStatus status;
    uint32_t creatorProgress;  // calculated, not stored
    uint32_t targetProgress;
};

class ChallengeManager {
public:
    explicit ChallengeManager(const QString& filePath,
                              DatabaseManager* db,
                              uint32_t localUserId);

    bool load();
    bool save();

    // create challenge
    bool createChallenge(uint32_t targetUserId, uint32_t targetMinutes,
                         const QDate& endDate);

    // respond
    bool acceptChallenge(uint32_t challengeId);
    bool rejectChallenge(uint32_t challengeId);

    // queries
    QVector<Challenge> incoming() const;   // challenges targeting local user
    QVector<Challenge> outgoing() const;   // challenges created by local user
    QVector<Challenge> active() const;     // accepted and not expired
    QVector<Challenge> history() const;    // completed/expired

    uint32_t localUserId() const { return m_localUserId; }
    void recalculateProgress(Challenge& c) const;
    bool refreshProgressAfterFocus();

private:
    QString m_filePath;
    DatabaseManager* m_db;
    uint32_t m_localUserId;
    QVector<Challenge> m_challenges;
    uint32_t nextId() const;
    uint32_t focusMinutesBetween(uint32_t userId, const QDate& from, const QDate& to) const;
};

#endif
