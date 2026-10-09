#include "core/ChallengeRewardService.h"
#include "core/CoinManager.h"
#include "core/FocusRecordSemantics.h"
#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include <QDateTime>
#include <algorithm>

namespace {
const QStringList taskIds = {QStringLiteral("focus-minutes"), QStringLiteral("focus-session"),
                             QStringLiteral("specified-plant"), QStringLiteral("growth")};
const QVector<int> checkinAmounts = {10, 14, 18, 22, 30};
}

ChallengeRewardService::Progress ChallengeRewardService::progress(
    const QDate& date, const QVector<Challenge>&, const std::vector<FocusRecord>& records)
{
    Progress result;
    result.date = date;
    if (!date.isValid()) {
        result.error = QStringLiteral("奖励日期无效。");
        result.monthlyState = RewardState::Unavailable;
        return result;
    }
    const auto& plants = PlantCatalog::all();
    const auto selectedPlant = plants.isEmpty() ? 0u : plants[date.month() % plants.size()].type;
    result.plantType = selectedPlant;
    uint64_t todaySeconds = 0, monthSeconds = 0, selectedSeconds = 0;
    int todayStages = 0, monthStages = 0;
    const QDate monthStart(date.year(), date.month(), 1);
    for (const auto& record : records) {
        if (record.status != FocusRecordStatus::Success) continue;
        const auto day = FocusRecordSemantics::startDate(record);
        if (day == date) {
            todaySeconds += record.actualSeconds;
            todayStages += static_cast<int>(record.growthStage) * 6;
            ++result.todaySessions;
            if (record.plantType == selectedPlant) selectedSeconds += record.actualSeconds;
        }
        if (day >= monthStart && day <= date) {
            monthSeconds += record.actualSeconds;
            monthStages += static_cast<int>(record.growthStage) * 6;
        }
    }
    result.todayMinutes = FocusRecordSemantics::wholeMinutes(todaySeconds);
    result.monthMinutes = FocusRecordSemantics::wholeMinutes(monthSeconds);
    result.monthTarget = std::max(1, date.daysInMonth() * 25);
    result.remainingDays = std::max(0, static_cast<int>(date.daysTo(QDate(date.year(), date.month(), date.daysInMonth()))));
    result.growth = result.monthMinutes * 2 + monthStages;
    result.todayGrowth = result.todayMinutes * 2 + todayStages;
    result.currentCheckin = (date.day() - 1) % checkinAmounts.size();
    for (int i = 0; i < checkinAmounts.size(); ++i) {
        result.checkins.append({checkinAmounts[i], i == result.currentCheckin ? RewardState::Available
            : (i < result.currentCheckin ? RewardState::Expired : RewardState::Locked)});
    }
    const int minuteTarget = std::max(25, result.monthTarget / 20);
    const int plantTarget = 30 + (date.day() % 4) * 10;
    const QString plantName = PlantCatalog::byType(selectedPlant).displayName;
    result.tasks = {
        {taskIds[0], result.todayMinutes, minuteTarget, 12, TaskType::FocusMinutes,
         QStringLiteral("累计专注 %1 分钟").arg(minuteTarget), QStringLiteral("每日专注"),
         QStringLiteral("今日成功完成专注的有效秒数累计后换算为整分钟。"), QStringLiteral("去专注"), 0, true},
        {taskIds[1], result.todaySessions, 1, 8, TaskType::FocusSession,
         QStringLiteral("完成一次专注"), QStringLiteral("基础任务"),
         QStringLiteral("今日成功完成一次专注；放弃、规则失败和中断不计入。"), QStringLiteral("去专注"), 0, false},
        {taskIds[2], FocusRecordSemantics::wholeMinutes(selectedSeconds), plantTarget, 16, TaskType::PlantMinutes,
         QStringLiteral("使用%1专注 %2 分钟").arg(plantName).arg(plantTarget), QStringLiteral("树种任务"),
         QStringLiteral("今日使用%1成功专注，累计有效整分钟达到目标。").arg(plantName),
         QStringLiteral("查看植物"), selectedPlant, true},
        {taskIds[3], result.todayGrowth, 120, 10, TaskType::Growth,
         QStringLiteral("今日累计成长值 120"), QStringLiteral("成长任务"),
         QStringLiteral("今日有效整分钟 × 2，加今日成功植物成长阶段 × 6。"),
         QStringLiteral("查看规则"), 0, false}
    };
    result.monthlyState = result.monthMinutes >= result.monthTarget ? RewardState::Available : RewardState::NotReady;
    for (auto& task : result.tasks)
        task.state = task.progress >= task.target ? RewardState::Available : RewardState::NotReady;
    return result;
}

ChallengeRewardService::Progress ChallengeRewardService::snapshot(
    const QDate& date, const QVector<Challenge>& challenges, const std::vector<FocusRecord>& records) const
{
    auto data = progress(date, challenges, records);
    if (!date.isValid()) return data;
    const auto& preferences = UserPreferences::instance();
    if (preferences.unresolvedLegacyRewards()) data.error = preferences.migrationNotice();
    const QDate today = QDate::currentDate();
    auto state = [&](Kind kind, int index, RewardState ready) {
        if (isClaimed(kind, index, date)) return RewardState::Claimed;
        if (!data.error.isEmpty()) return RewardState::Unavailable;
        if (date != today) {
            if (kind == Kind::Checkin) return date < today ? RewardState::Expired : RewardState::Locked;
            return RewardState::NotReady;
        }
        return ready;
    };
    data.monthlyState = state(Kind::Monthly, 0, data.monthlyState);
    for (int i = 0; i < data.tasks.size(); ++i) data.tasks[i].state = state(Kind::Daily, i, data.tasks[i].state);
    for (int i = 0; i < data.checkins.size(); ++i) data.checkins[i].state = state(Kind::Checkin, i, data.checkins[i].state);
    return data;
}

QString ChallengeRewardService::claimKey(Kind kind, int index, const QDate& date)
{
    if (!date.isValid()) return {};
    const QString day = date.toString("yyyyMMdd");
    if (kind == Kind::Checkin && index >= 0 && index < checkinAmounts.size())
        return QStringLiteral("challenge/checkin/%1/checkin-%2").arg(day).arg(index);
    if (kind == Kind::Daily && index >= 0 && index < taskIds.size())
        return QStringLiteral("challenge/daily/%1/%2").arg(day, taskIds[index]);
    if (kind == Kind::Monthly && index == 0)
        return QStringLiteral("challenge/monthly/%1/month-focus").arg(date.toString("yyyyMM"));
    return {};
}

bool ChallengeRewardService::synchronizeLegacy(QString* error)
{
    const auto& preferences = UserPreferences::instance();
    if (preferences.unresolvedLegacyRewards()) {
        if (error) *error = preferences.migrationNotice();
        return false;
    }
    if (!wallet_.importChallengeClaims(preferences.legacyChallengeClaimKeys())) {
        if (error) *error = QStringLiteral("旧领取凭据未能保存，未发放任何新奖励。请检查数据目录后重试。");
        return false;
    }
    return true;
}

bool ChallengeRewardService::isClaimed(Kind kind, int index, const QDate& date) const
{
    return wallet_.hasChallengeReward(claimKey(kind, index, date));
}

ChallengeRewardService::Result ChallengeRewardService::claim(
    Kind kind, int index, const QDate& date, const QVector<Challenge>& challenges,
    const std::vector<FocusRecord>& records)
{
    QString error;
    if (!synchronizeLegacy(&error)) return {Status::RecoveryRequired, 0, error};
    const auto key = claimKey(kind, index, date);
    if (key.isEmpty()) return {Status::NotReady, 0, QStringLiteral("奖励项目无效。")};
    if (wallet_.hasChallengeReward(key)) return {Status::AlreadyClaimed, 0, {}};
    if (date != QDate::currentDate()) return {Status::NotReady, 0, QStringLiteral("该奖励周期已过期，请刷新页面。")};
    const auto data = progress(date, challenges, records);
    int amount = 0;
    if (kind == Kind::Checkin) {
        if (index != data.currentCheckin)
            return {Status::NotReady, 0, QStringLiteral("该签到奖励尚未开放或已过期。")};
        amount = data.checkins[index].coins;
    } else if (kind == Kind::Daily) {
        const auto task = data.tasks[index];
        if (task.progress < task.target) return {Status::NotReady, 0, QStringLiteral("任务尚未完成。")};
        amount = task.coins;
    } else {
        if (data.monthMinutes < data.monthTarget) return {Status::NotReady, 0, QStringLiteral("本月挑战尚未完成。")};
        amount = data.monthlyCoins;
    }
    const auto saved = wallet_.grantChallengeReward(key, static_cast<uint32_t>(amount));
    if (saved == CoinManager::OperationResult::Applied) return {Status::Applied, amount, {}};
    if (saved == CoinManager::OperationResult::AlreadyApplied) return {Status::AlreadyClaimed, 0, {}};
    return {Status::SaveFailed, 0, QStringLiteral("奖励未能保存，金币与领取状态均未改变。请检查数据目录后重试。")};
}
