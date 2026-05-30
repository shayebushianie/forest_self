#include "core/CoinManager.h"
#include <iostream>

CoinManager::CoinManager(QObject* parent) : QObject(parent) {}
CoinManager::~CoinManager() { save(); }

void CoinManager::setDataPath(const std::string& path) { dataPath_ = path; }

bool CoinManager::load()
{
    std::ifstream file(dataPath_, std::ios::binary);
    WalletRecord rec;

    if (!file.is_open()) {
        balance_ = 0;
        totalCoins_ = 0;
        achievementMask_ = 0;
        return true;
    }

    file.read(reinterpret_cast<char*>(&rec), sizeof(WalletRecord));
    if (file.gcount() == sizeof(WalletRecord)) {
        balance_ = rec.totalCoins;
        totalCoins_ = rec.totalCoins;
        achievementMask_ = rec.unlockedAchievementMask;
    } else {
        // Legacy: old 4-byte coins.dat
        file.clear();
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(&balance_), sizeof(balance_));
        totalCoins_ = balance_;
        achievementMask_ = 0;
    }
    return true;
}

bool CoinManager::save()
{
    WalletRecord rec;
    rec.totalCoins = balance_;
    rec.unlockedPlantMask = 0;
    rec.unlockedSoundMask = 0;
    rec.unlockedAchievementMask = achievementMask_;

    std::ofstream file(dataPath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;
    file.write(reinterpret_cast<const char*>(&rec), sizeof(WalletRecord));
    return file.good();
}

bool CoinManager::earn(uint32_t amount)
{
    balance_ += amount;
    totalCoins_ += amount;
    save();
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::spend(uint32_t amount)
{
    if (amount > balance_) return false;
    balance_ -= amount;
    save();
    emit sig_balanceChanged(balance_);
    return true;
}

void CoinManager::setUnlockedAchievementMask(uint32_t mask)
{
    achievementMask_ = mask;
    save();
}
