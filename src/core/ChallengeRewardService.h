#ifndef CHALLENGE_REWARD_SERVICE_H
#define CHALLENGE_REWARD_SERVICE_H

#include "common/DatabaseCommon.h"
#include "core/ChallengeManager.h"
#include <QDate>
#include <QString>
#include <QVector>
#include <vector>

class CoinManager;

class ChallengeRewardService final {
public:
    enum class Kind { Checkin, Daily, Monthly };
    enum class Status { Applied, AlreadyClaimed, NotReady, SaveFailed, RecoveryRequired };
    struct Result { Status status; int coins = 0; QString error; };
    enum class RewardState { Unavailable, NotReady, Available, Claimed, Locked, Expired };
    enum class TaskType { FocusMinutes, FocusSession, PlantMinutes, Growth };
    struct Task {
        QString id;
        int progress = 0;
        int target = 1;
        int coins = 0;
        TaskType type = TaskType::FocusMinutes;
        QString title;
        QString category;
        QString description;
        QString actionText;
        uint32_t plantType = 0;
        bool bonus = false;
        RewardState state = RewardState::NotReady;
    };
    struct Checkin { int coins = 0; RewardState state = RewardState::Locked; };
    struct Progress {
        int todayMinutes = 0;
        int todaySessions = 0;
        int monthMinutes = 0;
        int monthTarget = 1;
        int growth = 0;
        int todayGrowth = 0;
        int monthlyCoins = 30;
        int remainingDays = 0;
        uint32_t plantType = 0;
        QVector<Task> tasks;
        QVector<Checkin> checkins;
        int currentCheckin = -1;
        RewardState monthlyState = RewardState::NotReady;
        QDate date;
        QString error;
    };

    explicit ChallengeRewardService(CoinManager& wallet) : wallet_(wallet) {}
    static Progress progress(const QDate& date, const QVector<Challenge>& challenges,
                             const std::vector<FocusRecord>& records);
    Progress snapshot(const QDate& date, const QVector<Challenge>& challenges,
                      const std::vector<FocusRecord>& records) const;
    static QString claimKey(Kind kind, int index, const QDate& date);
    bool synchronizeLegacy(QString* error = nullptr);
    bool isClaimed(Kind kind, int index, const QDate& date) const;
    Result claim(Kind kind, int index, const QDate& date, const QVector<Challenge>& challenges,
                 const std::vector<FocusRecord>& records);

private:
    CoinManager& wallet_;
};
#endif
