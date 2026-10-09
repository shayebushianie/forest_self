#include "UserFeatureServices.h"

#include <QDir>

#include "ChallengeManager.h"
#include "ForestLayoutManager.h"
#include "FocusSettlementManager.h"
#include "FocusController.h"
#include "CoinManager.h"
#include "ChallengeRewardService.h"
#include "config/UserPreferences.h"
#include "FriendManager.h"
#include "GachaManager.h"
#include "GuardianManager.h"
#include "TagManager.h"
#include "../common/PathConfig.h"
#include "../storage/DatabaseManager.h"
#include "../storage/UserManager.h"

UserFeatureServices::UserFeatureServices(const QString& userDir,
                                         uint32_t loggedUserId,
                                         UserManager& userManager,
                                         DatabaseManager& database)
{
    gacha_ = std::make_unique<GachaManager>(QDir(userDir).filePath("variants.dat"));
    forestLayout_ = std::make_unique<ForestLayoutManager>(QDir(userDir).filePath("forest_layout.dat"));
    friendManager_ = std::make_unique<FriendManager>(
        PathConfig::getAppDataFilePath("friend_reqs.dat"),
        QDir(userDir).filePath("friends.dat"),
        loggedUserId,
        userManager);
    challenge_ = std::make_unique<ChallengeManager>(
        PathConfig::getAppDataFilePath("challenges.dat"),
        &database,
        loggedUserId);
    tag_ = std::make_unique<TagManager>(QDir(userDir).filePath("tags.dat"));
    guardian_ = std::make_unique<GuardianManager>(QDir(userDir).filePath("guardian.dat"));
    settlements_ = std::make_unique<FocusSettlementManager>(QDir(userDir).filePath("focus_settlements.dat"), database);
}

UserFeatureServices::~UserFeatureServices() = default;

bool UserFeatureServices::load()
{
    lastError_.clear();
    if (!gacha_->load()) { lastError_ = gacha_->lastError(); return false; }
    if (!forestLayout_->load() || !friendManager_->load() || !challenge_->load() || !tag_->load()) {
        lastError_ = QStringLiteral("森林布局、好友、挑战或标签数据无法打开，请恢复完整数据快照。");
        return false;
    }
    if (!guardian_->load()) {
        lastError_ = guardian_->lastError().isEmpty() ? QStringLiteral("时间守护数据无法打开，请恢复完整数据快照。") : guardian_->lastError();
        return false;
    }
    if (!settlements_->load()) {
        lastError_ = QStringLiteral("专注结算凭据无法打开，请恢复完整数据快照。");
        return false;
    }
    if (!guardian_->checkDayBoundary()) {
        lastError_ = QStringLiteral("时间守护日边界保存失败：%1").arg(guardian_->lastError());
        return false;
    }
    return true;
}

bool UserFeatureServices::recoverEconomy(CoinManager& wallet, QString* notice, QString* error)
{
    if (notice) notice->clear();
    const auto recovered = gacha_->recover(wallet);
    if (!gacha_->lastError().isEmpty()) {
        if (error) *error = gacha_->lastError();
        return false;
    }
    ChallengeRewardService rewards(wallet);
    if (!UserPreferences::instance().unresolvedLegacyRewards() && !rewards.synchronizeLegacy(error)) return false;
    if (recovered && notice) {
        *notice = recovered->isNew
            ? QStringLiteral("已恢复上次异色发掘：%1 已解锁，未重复扣款。").arg(recovered->displayName)
            : QStringLiteral("已恢复上次异色发掘：%1，重复返还 %2 金币，未重复扣款或返还。")
                  .arg(recovered->displayName).arg(recovered->coinRefund);
    }
    return true;
}

bool UserFeatureServices::save()
{
    return gacha_->save() && forestLayout_->save() && challenge_->save() &&
           tag_->save() && guardian_->save() && settlements_->save();
}

bool UserFeatureServices::saveForBackup(const FocusController& focus, CoinManager& wallet, QString* error)
{
    const auto state = focus.currentState();
    if (focus.hasPendingFinalization() || state == FocusController::State::RUNNING || state == FocusController::State::PAUSED ||
        state == FocusController::State::WARNING) {
        if (error) *error = QStringLiteral("请在本次专注结束后创建备份，当前专注不会被中断。");
        return false;
    }
    if (!wallet.save() || !save()) {
        if (error) *error = QStringLiteral("当前数据未能完整保存，未创建备份。请检查数据目录权限后重试。");
        return false;
    }
    return true;
}

GachaManager* UserFeatureServices::gacha() const
{
    return gacha_.get();
}

ForestLayoutManager* UserFeatureServices::forestLayout() const
{
    return forestLayout_.get();
}

FriendManager* UserFeatureServices::friends() const
{
    return friendManager_.get();
}

ChallengeManager* UserFeatureServices::challenges() const
{
    return challenge_.get();
}

TagManager* UserFeatureServices::tags() const
{
    return tag_.get();
}

GuardianManager* UserFeatureServices::guardian() const
{
    return guardian_.get();
}

FocusSettlementManager* UserFeatureServices::settlements() const
{
    return settlements_.get();
}
