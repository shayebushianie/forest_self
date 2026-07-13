#ifndef FOCUS_RESULT_SERVICE_H
#define FOCUS_RESULT_SERVICE_H

#include <QVector>
#include <QString>
#include <cstdint>

#include "common/DatabaseCommon.h"
#include "core/AchievementEngine.h"

class ChallengeManager;
class CoinManager;
class DatabaseManager;
class ForestLayoutManager;
class GachaManager;
class GuardianManager;
class FocusSettlementManager;

// Applies all business effects of one persisted terminal focus record.
// UI code only consumes the returned summary and refreshes visible widgets.
class FocusResultService final {
public:
    struct Outcome {
        bool applied = false;
        bool alreadyApplied = false;
        bool completed = false;
        uint32_t recordId = 0;
        uint32_t focusCoins = 0;
        QVector<int> unlockedAchievements;
        QString error;
    };

    FocusResultService(DatabaseManager& database, CoinManager& coins,
                       AchievementEngine& achievements, GachaManager* gacha,
                       ForestLayoutManager* forestLayout, GuardianManager* guardian,
                       ChallengeManager* challenges,
                       FocusSettlementManager* settlements = nullptr);

    bool registerPendingRecord(uint32_t recordId);
    Outcome applyTerminalRecord(uint32_t recordId);
    QVector<Outcome> recoverPendingRecords();
    QVector<int> evaluateAchievements(const FocusRecord* latestRecord = nullptr);

private:
    AchievementEngine::Context buildAchievementContext(const FocusRecord* latestRecord) const;

    DatabaseManager& database_;
    CoinManager& coins_;
    AchievementEngine& achievements_;
    GachaManager* gacha_ = nullptr;
    ForestLayoutManager* forestLayout_ = nullptr;
    GuardianManager* guardian_ = nullptr;
    ChallengeManager* challenges_ = nullptr;
    FocusSettlementManager* settlements_ = nullptr;
    QSet<uint32_t> appliedRecordIds_;
    bool lastAchievementEvaluationSucceeded_ = true;
};

#endif // FOCUS_RESULT_SERVICE_H
#include <QSet>
