#include "core/FocusResultService.h"

#include "core/AchievementEngine.h"
#include "core/ChallengeManager.h"
#include "core/CoinManager.h"
#include "core/ForestLayoutManager.h"
#include "core/FocusSettlementManager.h"
#include "core/GachaManager.h"
#include "core/GuardianManager.h"
#include "config/PlantCatalog.h"
#include "core/FocusRecordSemantics.h"
#include "storage/DatabaseManager.h"
#include "utils/Logger.h"

#include <algorithm>

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
    const bool saved = !settlements_ || settlements_->markPending(recordId);
    if (!saved)
        Logger::instance().error("record=" + std::to_string(recordId) + " pending entry save failed.",
                                 "settlement", "pending-save");
    return saved;
}

FocusResultService::Outcome FocusResultService::applyTerminalRecord(uint32_t recordId)
{
    Outcome outcome;
    outcome.recordId = recordId;
    const auto record = database_.readById(recordId);
    if (!record || !isTerminalFocusStatus(record->status)) {
        Logger::instance().error("record=" + std::to_string(recordId) + " terminal read failed or not ready.",
                                 "settlement", "read");
        outcome.error = database_.lastError().isEmpty() ? QStringLiteral("专注结果尚未完成持久化")
                                                       : database_.lastError();
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
        Logger::instance().error("record=" + std::to_string(recordId) + " pending entry save failed.",
                                 "settlement", "pending-save");
        outcome.error = QStringLiteral("无法记录待结算专注");
        return outcome;
    }

    bool fullyApplied = true;
    auto addError = [&](const QString& error) {
        fullyApplied = false;
        if (!outcome.error.isEmpty()) outcome.error += QStringLiteral("；");
        outcome.error += error;
    };
    auto reportFailure = [&](const char* operation) {
        Logger::instance().error("record=" + std::to_string(recordId) + " settlement remains pending.",
                                 "settlement", operation);
    };

    if (outcome.completed && outcome.focusCoins > 0 && !coins_.grantFocusReward(recordId, outcome.focusCoins)) {
        addError(QStringLiteral("专注奖励保存失败"));
        reportFailure("wallet-save");
    }
    if (outcome.completed && guardian_) {
        if (!guardian_->addCompletedFocusSeconds(recordId, record->actualSeconds,
                                                  FocusRecordSemantics::startDate(*record))) {
            addError(QStringLiteral("时间守护保存失败"));
            reportFailure("guardian-save");
        }
    }
    if (challenges_ && !challenges_->refreshProgressAfterFocus()) {
        addError(QStringLiteral("挑战进度保存失败"));
        reportFailure("challenge-save");
    }

    outcome.unlockedAchievements = evaluateAchievements(&record.value());
    if (!lastAchievementEvaluationSucceeded_) {
        reportFailure("achievement-check");
        addError(database_.lastError().isEmpty() ? QStringLiteral("成就状态保存失败")
            : QStringLiteral("成就历史读取失败：%1").arg(database_.lastError()));
    }
    if (fullyApplied && settlements_ && !settlements_->markSettled(recordId)) {
        addError(QStringLiteral("专注结算状态保存失败"));
        reportFailure("ledger-save");
    }
    if (fullyApplied && !settlements_) appliedRecordIds_.insert(recordId);
    outcome.applied = fullyApplied;
    if (fullyApplied)
        Logger::instance().info("record=" + std::to_string(recordId) + " settlement confirmed.",
                                "settlement", "applied");
    return outcome;
}

QVector<FocusResultService::Outcome> FocusResultService::recoverPendingRecords()
{
    QVector<Outcome> outcomes;
    if (!settlements_) return outcomes;
    std::vector<FocusRecord> records;
    if (!database_.getAllRecords(records)) {
        Logger::instance().error("History query failed; recovery stopped.", "settlement", "recover-read");
        Outcome failed;
        failed.error = QStringLiteral("无法读取专注历史，已停止恢复结算：%1").arg(database_.lastError());
        outcomes.push_back(failed);
        return outcomes;
    }
    const auto confirmedRecordIds = settlements_->confirmedRecordIds();
    for (uint32_t recordId : confirmedRecordIds) {
        const auto record = database_.readById(recordId);
        if (!database_.lastError().isEmpty()) {
            Logger::instance().error("record=" + std::to_string(recordId) + " receipt read failed.",
                                     "settlement", "recover-read");
            Outcome failed;
            failed.recordId = recordId;
            failed.error = database_.lastError();
            outcomes.push_back(failed);
            return outcomes;
        }
        if (!record || !isTerminalFocusStatus(record->status) ||
            (record->status == FocusRecordStatus::Success &&
             ((record->coinsEarned > 0 && !coins_.hasFocusReward(recordId)) ||
              (guardian_ && (record->actualSeconds >= 60 || !guardian_->needsHistoryReconciliation()) &&
               !guardian_->hasCompletedFocus(recordId))))) {
            Outcome inconsistent;
            Logger::instance().error("record=" + std::to_string(recordId) +
                                     " receipt mismatch; recovery stopped.", "settlement", "recover-validate");
            inconsistent.recordId = recordId;
            inconsistent.error = QStringLiteral("已结算凭据与钱包或守护不一致，无法证明历史购买、退款及守护日期是否完整。已停止自动补偿，请恢复完整数据快照。");
            outcomes.push_back(inconsistent);
            return outcomes;
        }
    }
    if (guardian_ && guardian_->needsHistoryReconciliation() &&
        !guardian_->reconcileCompletedFocusRecords(records)) {
        Outcome failed;
        Logger::instance().error("Guardian reconciliation failed; recovery stopped.", "settlement", "recover-guardian");
        failed.error = QStringLiteral("时间守护状态重算或保存失败");
        outcomes.push_back(failed);
        return outcomes;
    }
    const auto pending = settlements_->pendingRecordIds();
    if (!pending.isEmpty())
        Logger::instance().info("pending=" + std::to_string(pending.size()), "settlement", "recover-start");
    for (uint32_t recordId : pending) {
        outcomes.push_back(applyTerminalRecord(recordId));
    }
    if (!pending.isEmpty()) {
        const bool recovered = std::all_of(outcomes.begin(), outcomes.end(), [](const Outcome& item) { return item.applied; });
        if (recovered) Logger::instance().info("Pending settlement recovery completed.", "settlement", "recover-complete");
        else Logger::instance().error("Pending settlement recovery incomplete.", "settlement", "recover-incomplete");
    }
    return outcomes;
}

QVector<int> FocusResultService::evaluateAchievements(const FocusRecord* latestRecord)
{
    std::vector<FocusRecord> records;
    lastAchievementEvaluationSucceeded_ = database_.getAllRecords(records);
    if (!lastAchievementEvaluationSucceeded_) return {};
    AchievementEngine::Context context = buildAchievementContext(records, latestRecord);
    QVector<int> unlocked;
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

AchievementEngine::Context FocusResultService::buildAchievementContext(
    const std::vector<FocusRecord>& records, const FocusRecord* latestRecord) const
{
    AchievementEngine::Context context;
    for (const FocusRecord& record : records) {
        if (record.status == FocusRecordStatus::Success) {
            ++context.totalSuccessCount;
            context.totalSeconds += static_cast<int>(record.actualSeconds);
            if (PlantCatalog::isKnown(record.plantType)) context.plantedPlantTypes.insert(record.plantType);
        } else if (record.status == FocusRecordStatus::Failed ||
                   record.status == FocusRecordStatus::Abandoned) {
            ++context.totalFailCount;
        }
    }
    context.totalSessions = context.totalSuccessCount;

    if (latestRecord && latestRecord->status == FocusRecordStatus::Success) {
        context.lastSessionSuccess = true;
        context.lastPlantType = plantTypeName(latestRecord->plantType);
        context.lastSessionDuration = static_cast<int>(latestRecord->actualSeconds);
        context.lastSessionViolations = static_cast<int>(latestRecord->violationCount);
        context.lastSessionStrict = latestRecord->focusMode == PersistedFocusMode::Deep;
    }
    context.totalCoinsEarned = static_cast<int>(coins_.getTotalCoins());
    const uint32_t plantMask = coins_.getUnlockedPlantMask();
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        if (plant.type < 32 && (plantMask & (uint32_t{1} << plant.type)) != 0)
            ++context.unlockedPlantCount;
    }
    context.variantCount = gacha_ ? gacha_->unlockedCount() : 0;
    context.availableVariantCount = gacha_ ? gacha_->variantCount() : 0;
    context.forestPlacementCount = forestLayout_ ? forestLayout_->placements().size() : 0;
    return context;
}
