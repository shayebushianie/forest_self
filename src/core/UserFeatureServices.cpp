#include "UserFeatureServices.h"

#include <QDir>

#include "ChallengeManager.h"
#include "ForestLayoutManager.h"
#include "FocusSettlementManager.h"
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
    const bool loaded = gacha_->load() && forestLayout_->load() && friendManager_->load() &&
                        challenge_->load() && tag_->load() && guardian_->load() && settlements_->load();
    if (!loaded) return false;
    guardian_->checkDayBoundary();
    return true;
}

bool UserFeatureServices::save()
{
    return gacha_->save() && forestLayout_->save() && challenge_->save() &&
           tag_->save() && guardian_->save() && settlements_->save();
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
