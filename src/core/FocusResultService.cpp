#include "core/FocusResultService.h"

#include "core/AchievementEngine.h"
#include "core/ChallengeManager.h"
#include "core/CoinManager.h"
#include "core/ForestLayoutManager.h"
#include "core/FocusSettlementManager.h"
#include "core/GachaManager.h"
#include "core/GuardianManager.h"
#include "config/PlantCatalog.h"
#include "storage/DatabaseManager.h"

namespace {

QString plantTypeName(uint32_t plantType)
{
    return PlantCatalog::isKnown(plantType) ? PlantCatalog::byType(plantType).internalName : QString();
}

} // namespace

FocusResultService::FocusResultService(DatabaseManager& database, CoinManager& coins,
                                       AchievementEngine& achievements, GachaManager* gacha,
                                       ForestLayoutManager* forestLayout, GuardianManager* guardian,
                                       ChallengeManager* challenges,
                                       FocusSettlementManager* settlements)
    : database_(database), coins_(coins), achievements_(achievements), gacha_(gacha),
      forestLayout_(forestLayout), guardian_(guardian), challenges_(challenges), settlements_(settlements)
{
}

bool FocusResultService::registerPendingRecord(uint32_t recordId)
{
    return !settlements_ || settlements_->markPending(recordId);
}

FocusResultService::Outcome FocusResultService::applyTerminalRecord(uint32_t recordId)
{
    Outcome outcome;
    outcome.recordId = recordId;
    const auto record = database_.readById(recordId);
    if (!record || !isTerminalFocusStatus(record->status)) {
        outcome.error = QStringLiteral("专注结果尚未完成持久化");
        return outcome;
    }

    outcome.completed = record->status == FocusRecordStatus::Success;
    outcome.focusCoins = outcome.completed ? record->coinsEarned : 0;
    if (settlements_ && settlements_->isSettled(recordId)) {
        outcome.applied = true;
        outcome.alreadyApplied = true;
        return outcome;
    }
    if (!settlements_ && appliedRecordIds_.contains(recordId)) {
        outcome.applied = true;
        outcome.alreadyApplied = true;
        return outcome;
    }
    if (settlements_ && !settlements_->isPending(recordId) && !settlements_->markPending(recordId)) {
        outcome.error = QStringLiteral("无法记录待结算专注");
        return outcome;
    }

    bool fullyApplied = true;
    auto addError = [&](const QString& error) {
        fullyApplied = false;
        if (!outcome.error.isEmpty()) outcome.error += QStringLiteral("；");
        outcome.error += error;
    };

    if (outcome.completed && outcome.focusCoins > 0 && !coins_.grantFocusReward(recordId, outcome.focusCoins)) {
        addError(QStringLiteral("专注奖励保存失败"));
    }
    if (outcome.completed && guardian_) {
        const uint32_t minutes = record->actualSeconds / 60;
        if (minutes > 0 && !guardian_->addCompletedFocusMinutes(recordId, minutes)) {
            addError(QStringLiteral("时间守护保存失败"));
        }
    }
    if (challenges_ && !challenges_->refreshProgressAfterFocus()) {
        addError(QStringLiteral("挑战进度保存失败"));
    }

    outcome.unlockedAchievements = evaluateAchievements(&record.value());
    if (!lastAchievementEvaluationSucceeded_) {
        addError(QStringLiteral("成就状态保存失败"));
    }
    if (fullyApplied && settlements_ && !settlements_->markSettled(recordId)) {
        addError(QStringLiteral("专注结算状态保存失败"));
    }
    if (fullyApplied && !settlements_) appliedRecordIds_.insert(recordId);
    outcome.applied = fullyApplied;
    return outcome;
}

QVector<FocusResultService::Outcome> FocusResultService::recoverPendingRecords()
{
    QVector<Outcome> outcomes;
    if (!settlements_) return outcomes;
    for (uint32_t recordId : settlements_->pendingRecordIds()) {
        outcomes.push_back(applyTerminalRecord(recordId));
    }
    return outcomes;
}

QVector<int> FocusResultService::evaluateAchievements(const FocusRecord* latestRecord)
{
    AchievementEngine::Context context = buildAchievementContext(latestRecord);
    QVector<int> unlocked;
    lastAchievementEvaluationSucceeded_ = true;
    for (int pass = 0; pass < achievements_.count(); ++pass) {
        const QVector<int> newlyUnlocked = achievements_.checkAll(context);
        if (!achievements_.lastCheckSucceeded()) {
            lastAchievementEvaluationSucceeded_ = false;
        }
        if (newlyUnlocked.isEmpty()) break;
        unlocked += newlyUnlocked;
        context.totalCoinsEarned = static_cast<int>(coins_.getTotalCoins());
    }
    return unlocked;
}

AchievementEngine::Context FocusResultService::buildAchievementContext(const FocusRecord* latestRecord) const
{
    AchievementEngine::Context context;
    const auto records = database_.getAllRecords();
    context.totalSessions = static_cast<int>(records.size());
    for (const FocusRecord& record : records) {
        if (record.status == FocusRecordStatus::Success) {
            ++context.totalSuccessCount;
            context.totalSeconds += static_cast<int>(record.actualSeconds);
            if (record.plantType == 0) context.plantedOak = true;
            if (record.plantType == 1) context.plantedPine = true;
            if (record.plantType == 2) context.plantedRose = true;
            if (record.plantType == 3) context.plantedGinkgo = true;
            if (record.plantType == 4) context.plantedSunflower = true;
            if (record.plantType == 5) context.plantedCactus = true;
        } else if (record.status == FocusRecordStatus::Failed ||
                   record.status == FocusRecordStatus::Abandoned) {
            ++context.totalFailCount;
        }
    }

    if (latestRecord && latestRecord->status == FocusRecordStatus::Success) {
        context.lastSessionSuccess = true;
        context.lastPlantType = plantTypeName(latestRecord->plantType);
        context.lastSessionDuration = static_cast<int>(latestRecord->actualSeconds);
        context.lastSessionViolations = static_cast<int>(latestRecord->violationCount);
        context.lastSessionStrict = latestRecord->focusMode == PersistedFocusMode::Deep;
    }
    context.totalCoinsEarned = static_cast<int>(coins_.getTotalCoins());
    const uint32_t plantMask = coins_.getUnlockedPlantMask();
    for (int i = 0; i < 6; ++i) {
        if ((plantMask & (1u << i)) != 0) ++context.unlockedPlantCount;
    }
    context.variantCount = gacha_ ? gacha_->unlockedCount() : 0;
    context.forestPlacementCount = forestLayout_ ? forestLayout_->placements().size() : 0;
    return context;
}
