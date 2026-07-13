#include "core/CoinManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

#include <algorithm>
#include <cstring>
#include <iostream>

namespace {

constexpr quint32 kWalletMagic = 0x46574C31u; // FWL1
constexpr quint16 kWalletVersion = 2;

QString storagePath(const std::string& path)
{
    return QString::fromStdString(path);
}

QByteArray encodeWallet(uint32_t balance, uint32_t totalCoins, uint32_t achievementMask,
                        uint32_t unlockedPlantMask, const QSet<uint32_t>& focusRewardRecordIds)
{
    QList<uint32_t> recordIds = focusRewardRecordIds.values();
    std::sort(recordIds.begin(), recordIds.end());
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << balance << totalCoins << achievementMask << unlockedPlantMask
           << static_cast<quint32>(recordIds.size());
    for (uint32_t recordId : recordIds) stream << recordId;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

bool decodeWalletV2(const QByteArray& bytes, uint32_t& balance, uint32_t& totalCoins,
                    uint32_t& achievementMask, uint32_t& unlockedPlantMask,
                    QSet<uint32_t>& focusRewardRecordIds)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> balance >> totalCoins >> achievementMask >> unlockedPlantMask >> count;
    if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
    QSet<uint32_t> ids;
    for (quint32 i = 0; i < count; ++i) {
        uint32_t recordId = 0;
        stream >> recordId;
        if (stream.status() != QDataStream::Ok) return false;
        ids.insert(recordId);
    }
    if (!buffer.atEnd()) return false;
    focusRewardRecordIds = std::move(ids);
    return true;
}

}

CoinManager::CoinManager(QObject* parent) : QObject(parent) {}
CoinManager::~CoinManager() = default;

void CoinManager::setDataPath(const std::string& path) { dataPath_ = path; }

bool CoinManager::load()
{
    WalletRecord legacyRecord;
    const auto result = StorageEnvelope::read(storagePath(dataPath_), kWalletMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) {
        balance_ = 0;
        totalCoins_ = 0;
        achievementMask_ = 0;
        unlockedPlantMask_ = 1u;
        focusRewardRecordIds_.clear();
        return save();
    }

    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile legacy(storagePath(dataPath_));
        if (!legacy.open(QIODevice::ReadOnly)) return false;
        bytes = legacy.readAll();
    } else if (result.hasPayload()) {
        bytes = result.payload;
    } else {
        return false;
    }

    bool needsMigration = result.state == StorageEnvelope::ReadState::Legacy || result.version == 1;
    if (result.version == kWalletVersion && result.hasPayload()) {
        if (!decodeWalletV2(bytes, balance_, totalCoins_, achievementMask_, unlockedPlantMask_,
                            focusRewardRecordIds_)) return false;
    } else if (bytes.size() == static_cast<qsizetype>(sizeof(WalletRecord))) {
        std::memcpy(&legacyRecord, bytes.constData(), sizeof(legacyRecord));
        balance_ = legacyRecord.balance;
        totalCoins_ = legacyRecord.totalEarnedCoins == 0 ? legacyRecord.balance : legacyRecord.totalEarnedCoins;
        achievementMask_ = legacyRecord.unlockedAchievementMask;
        unlockedPlantMask_ = legacyRecord.unlockedPlantMask;
        focusRewardRecordIds_.clear();
        needsMigration = true;
    } else if (bytes.size() == static_cast<qsizetype>(sizeof(balance_))) {
        std::memcpy(&balance_, bytes.constData(), sizeof(balance_));
        totalCoins_ = balance_;
        achievementMask_ = 0;
        unlockedPlantMask_ = 1u;
        focusRewardRecordIds_.clear();
        needsMigration = true;
    } else if (bytes.isEmpty() && result.state == StorageEnvelope::ReadState::Legacy) {
        balance_ = 0;
        totalCoins_ = 0;
        achievementMask_ = 0;
        unlockedPlantMask_ = 1u;
        focusRewardRecordIds_.clear();
        needsMigration = true;
    } else {
        return false;
    }

    if (unlockedPlantMask_ == 0) unlockedPlantMask_ = 1u;
    return needsMigration ? save() : true;
}

bool CoinManager::save()
{
    return saveState(balance_, totalCoins_, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_);
}

bool CoinManager::saveState(uint32_t balance, uint32_t totalCoins,
                            uint32_t achievementMask, uint32_t unlockedPlantMask,
                            const QSet<uint32_t>& focusRewardRecordIds) const
{
    QString error;
    const bool saved = StorageEnvelope::write(storagePath(dataPath_), kWalletMagic, kWalletVersion,
                                              encodeWallet(balance, totalCoins, achievementMask,
                                                           unlockedPlantMask, focusRewardRecordIds), &error);
    if (!saved) std::cerr << "[WALLET SAVE ERROR] " << error.toStdString() << "\n";
    return saved;
}

bool CoinManager::earn(uint32_t amount)
{
    const uint32_t newBalance = balance_ + amount;
    const uint32_t newTotal = totalCoins_ + amount;
    if (!saveState(newBalance, newTotal, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_)) return false;
    balance_ = newBalance;
    totalCoins_ = newTotal;
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::grantFocusReward(uint32_t recordId, uint32_t amount)
{
    if (focusRewardRecordIds_.contains(recordId)) return true;
    QSet<uint32_t> updatedIds = focusRewardRecordIds_;
    updatedIds.insert(recordId);
    const uint32_t newBalance = balance_ + amount;
    const uint32_t newTotal = totalCoins_ + amount;
    if (!saveState(newBalance, newTotal, achievementMask_, unlockedPlantMask_, updatedIds)) return false;
    focusRewardRecordIds_ = std::move(updatedIds);
    balance_ = newBalance;
    totalCoins_ = newTotal;
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::refund(uint32_t amount)
{
    const uint32_t newBalance = balance_ + amount;
    if (!saveState(newBalance, totalCoins_, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_)) return false;
    balance_ = newBalance;
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::spend(uint32_t amount)
{
    if (amount > balance_) return false;
    const uint32_t newBalance = balance_ - amount;
    if (!saveState(newBalance, totalCoins_, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_)) return false;
    balance_ = newBalance;
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::setUnlockedAchievementMask(uint32_t mask)
{
    if (!saveState(balance_, totalCoins_, mask, unlockedPlantMask_, focusRewardRecordIds_)) return false;
    achievementMask_ = mask;
    return true;
}

bool CoinManager::grantAchievement(uint8_t storageBit, uint32_t rewardCoins)
{
    if (storageBit >= 32) return false;
    const uint32_t bit = 1u << storageBit;
    if ((achievementMask_ & bit) != 0) return true;

    const uint32_t newBalance = balance_ + rewardCoins;
    const uint32_t newTotal = totalCoins_ + rewardCoins;
    const uint32_t newMask = achievementMask_ | bit;
    if (!saveState(newBalance, newTotal, newMask, unlockedPlantMask_, focusRewardRecordIds_)) return false;
    balance_ = newBalance;
    totalCoins_ = newTotal;
    achievementMask_ = newMask;
    emit sig_balanceChanged(balance_);
    return true;
}

bool CoinManager::isPlantUnlocked(uint32_t plantType) const
{
    return (unlockedPlantMask_ & (1u << plantType)) != 0;
}

bool CoinManager::unlockPlant(uint32_t plantType)
{
    const uint32_t newMask = unlockedPlantMask_ | (1u << plantType);
    if (!saveState(balance_, totalCoins_, achievementMask_, newMask, focusRewardRecordIds_)) return false;
    unlockedPlantMask_ = newMask;
    return true;
}
