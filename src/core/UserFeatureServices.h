#ifndef USER_FEATURE_SERVICES_H
#define USER_FEATURE_SERVICES_H

#include <QString>
#include <cstdint>
#include <memory>

class DatabaseManager;
class UserManager;
class GachaManager;
class ForestLayoutManager;
class FriendManager;
class ChallengeManager;
class TagManager;
class GuardianManager;
class FocusSettlementManager;

// Owns feature managers that are scoped to the currently logged-in user.
// Keeping their lifecycle here prevents the application entry point from
// duplicating storage-path, load, and save orchestration.
class UserFeatureServices final {
public:
    UserFeatureServices(const QString& userDir,
                        uint32_t loggedUserId,
                        UserManager& userManager,
                        DatabaseManager& database);
    ~UserFeatureServices();

    bool load();
    bool save();

    GachaManager* gacha() const;
    ForestLayoutManager* forestLayout() const;
    FriendManager* friends() const;
    ChallengeManager* challenges() const;
    TagManager* tags() const;
    GuardianManager* guardian() const;
    FocusSettlementManager* settlements() const;

private:
    std::unique_ptr<GachaManager> gacha_;
    std::unique_ptr<ForestLayoutManager> forestLayout_;
    std::unique_ptr<FriendManager> friendManager_;
    std::unique_ptr<ChallengeManager> challenge_;
    std::unique_ptr<TagManager> tag_;
    std::unique_ptr<GuardianManager> guardian_;
    std::unique_ptr<FocusSettlementManager> settlements_;
};

#endif // USER_FEATURE_SERVICES_H
