#ifndef COINMANAGER_H
#define COINMANAGER_H

#include <QObject>
#include <QSet>
#include <cstdint>
#include <cstring>
#include <string>

#pragma pack(push, 1)
struct WalletRecord {
    uint32_t balance;
    uint32_t unlockedPlantMask;
    uint32_t unlockedSoundMask;
    uint32_t unlockedAchievementMask;
    uint32_t totalEarnedCoins;
    char     reserved[44];

    WalletRecord() { std::memset(this, 0, sizeof(WalletRecord)); }
};
#pragma pack(pop)

static_assert(sizeof(WalletRecord) == 64, "WalletRecord must be 64 bytes");

// Owns wallet persistence and reward balance changes. Achievements also use
// this file to store an unlocked-bit mask.
class CoinManager : public QObject {
    Q_OBJECT

public:
    explicit CoinManager(QObject* parent = nullptr);
    ~CoinManager() override;

    void setDataPath(const std::string& path);
    bool load();
    bool save();

    uint32_t balance() const { return balance_; }
    bool earn(uint32_t amount);
    bool grantFocusReward(uint32_t recordId, uint32_t amount);
    bool refund(uint32_t amount);
    bool spend(uint32_t amount);

    uint32_t getUnlockedAchievementMask() const { return achievementMask_; }
    bool setUnlockedAchievementMask(uint32_t mask);
    bool grantAchievement(uint8_t storageBit, uint32_t rewardCoins);
    uint32_t getTotalCoins() const { return totalCoins_; }

    bool isPlantUnlocked(uint32_t plantType) const;
    bool unlockPlant(uint32_t plantType);
    uint32_t getUnlockedPlantMask() const { return unlockedPlantMask_; }

signals:
    void sig_balanceChanged(uint32_t newBalance);

private:
    bool saveState(uint32_t balance, uint32_t totalCoins,
                   uint32_t achievementMask, uint32_t unlockedPlantMask,
                   const QSet<uint32_t>& focusRewardRecordIds) const;

    std::string dataPath_;
    uint32_t balance_ = 0;
    uint32_t totalCoins_ = 0;
    uint32_t achievementMask_ = 0;
    uint32_t unlockedPlantMask_ = 0;
    QSet<uint32_t> focusRewardRecordIds_;
};

#endif // COINMANAGER_H
