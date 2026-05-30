#ifndef COINMANAGER_H
#define COINMANAGER_H

#include <QObject>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

#pragma pack(push, 1)
struct WalletRecord {
    uint32_t totalCoins;
    uint32_t unlockedPlantMask;
    uint32_t unlockedSoundMask;
    uint32_t unlockedAchievementMask;
    char     reserved[48];

    WalletRecord() { std::memset(this, 0, sizeof(WalletRecord)); }
};
#pragma pack(pop)

static_assert(sizeof(WalletRecord) == 64, "WalletRecord must be 64 bytes");

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
    bool spend(uint32_t amount);

    uint32_t getUnlockedAchievementMask() const { return achievementMask_; }
    void setUnlockedAchievementMask(uint32_t mask);
    uint32_t getTotalCoins() const { return totalCoins_; }

signals:
    void sig_balanceChanged(uint32_t newBalance);

private:
    std::string dataPath_;
    uint32_t balance_ = 0;
    uint32_t totalCoins_ = 0;
    uint32_t achievementMask_ = 0;
};

#endif // COINMANAGER_H
