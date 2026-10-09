#include "core/CoinManager.h"
#include "config/PlantCatalog.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

constexpr quint32 kWalletMagic = 0x46574C31u; // FWL1
constexpr quint16 kWalletVersion = 3;

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

bool decodeWalletV2(QBuffer& buffer, uint32_t& balance, uint32_t& totalCoins,
                    uint32_t& achievementMask, uint32_t& unlockedPlantMask,
                    QSet<uint32_t>& focusRewardRecordIds)
{
    QDataStream stream(&buffer);
    quint32 count = 0;
    stream >> balance >> totalCoins >> achievementMask >> unlockedPlantMask >> count;
    if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
    QSet<uint32_t> ids;
    for (quint32 i = 0; i < count; ++i) {
        uint32_t recordId = 0;
        stream >> recordId;
        if (stream.status() != QDataStream::Ok) return false;
        if (ids.contains(recordId)) return false;
        ids.insert(recordId);
    }
    focusRewardRecordIds = std::move(ids);
    return true;
}

}

CoinManager::CoinManager(QObject* parent) : QObject(parent) {}
CoinManager::~CoinManager() = default;

void CoinManager::setDataPath(const std::string& path) { dataPath_ = path; }

bool CoinManager::load()
{
    lastError_.clear();
    receipts_.clear();
    WalletRecord legacyRecord;
    // A wallet-only fallback can lose purchases or claims while other modules remain newer.
    const auto result = StorageEnvelope::inspect(storagePath(dataPath_), kWalletMagic);
    if (result.state == StorageEnvelope::ReadState::Corrupt ||
        (result.state == StorageEnvelope::ReadState::Legacy &&
         StorageEnvelope::inspect(storagePath(dataPath_) + ".bak", kWalletMagic).hasPayload()) ||
        (result.state == StorageEnvelope::ReadState::Missing && QFile::exists(storagePath(dataPath_) + ".bak"))) {
        lastError_ = QStringLiteral("金币主文件损坏或缺失，不能单独回退旧钱包。请通过数据与备份恢复完整快照；原文件和 .bak 已保留。");
        return false;
    }
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
        lastError_ = QStringLiteral("金币数据内容无效，未尝试回退或重置。请恢复完整数据快照。");
        return false;
    }

    if (result.hasPayload() && (result.version < 1 || result.version > kWalletVersion)) {
        lastError_ = QStringLiteral("金币数据版本不受支持，请使用兼容版本或恢复完整快照。");
        return false;
    }
    bool needsMigration = result.state == StorageEnvelope::ReadState::Legacy || result.version < kWalletVersion;
    if (result.version >= 2 && result.hasPayload()) {
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::ReadOnly) ||
            !decodeWalletV2(buffer, balance_, totalCoins_, achievementMask_, unlockedPlantMask_,
                            focusRewardRecordIds_)) return false;
        if (result.version == kWalletVersion) {
            QDataStream stream(&buffer);
            quint32 count = 0;
            stream >> count;
            if (stream.status() != QDataStream::Ok || count > 1000000u) return false;
            for (quint32 index = 0; index < count; ++index) {
                QString key;
                Receipt receipt;
                stream >> key >> receipt.detail >> receipt.cost >> receipt.amount >> receipt.isNew;
                if (stream.status() != QDataStream::Ok || key.isEmpty() || key.size() > 256 ||
                    receipt.detail.size() > 256 || receipts_.contains(key) ||
                    (!key.startsWith("challenge/") && !key.startsWith("gacha/"))) return false;
                receipts_.insert(key, receipt);
            }
        }
        if (!buffer.atEnd()) return false;
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
                            const QSet<uint32_t>& focusRewardRecordIds,
                            const QMap<QString, Receipt>* receipts) const
{
    QByteArray payload = encodeWallet(balance, totalCoins, achievementMask, unlockedPlantMask,
                                     focusRewardRecordIds);
    QBuffer buffer(&payload);
    if (!buffer.open(QIODevice::WriteOnly | QIODevice::Append)) return false;
    QDataStream stream(&buffer);
    const auto& savedReceipts = receipts ? *receipts : receipts_;
    stream << static_cast<quint32>(savedReceipts.size());
    for (auto it = savedReceipts.cbegin(); it != savedReceipts.cend(); ++it)
        stream << it.key() << it->detail << it->cost << it->amount << it->isNew;
    if (stream.status() != QDataStream::Ok) return false;
    buffer.close();
    QString error;
    const bool saved = StorageEnvelope::write(storagePath(dataPath_), kWalletMagic, kWalletVersion,
                                              payload, &error);
    lastError_ = error;
    if (!saved) std::cerr << "[WALLET SAVE ERROR] " << error.toStdString() << "\n";
    return saved;
}

bool CoinManager::earn(uint32_t amount)
{
    if (amount > std::numeric_limits<uint32_t>::max() - balance_ ||
        amount > std::numeric_limits<uint32_t>::max() - totalCoins_) return false;
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
    if (amount > std::numeric_limits<uint32_t>::max() - balance_ ||
        amount > std::numeric_limits<uint32_t>::max() - totalCoins_) return false;
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
    if (amount > std::numeric_limits<uint32_t>::max() - balance_) return false;
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
    if (rewardCoins > std::numeric_limits<uint32_t>::max() - balance_ ||
        rewardCoins > std::numeric_limits<uint32_t>::max() - totalCoins_) return false;

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
    if (!PlantCatalog::isKnown(plantType) || plantType >= 32) return false;
    return (unlockedPlantMask_ & (1u << plantType)) != 0;
}

bool CoinManager::unlockPlant(uint32_t plantType)
{
    if (!PlantCatalog::isKnown(plantType) || plantType >= 32) return false;
    const uint32_t newMask = unlockedPlantMask_ | (1u << plantType);
    if (!saveState(balance_, totalCoins_, achievementMask_, newMask, focusRewardRecordIds_)) return false;
    unlockedPlantMask_ = newMask;
    return true;
}

CoinManager::OperationResult CoinManager::purchasePlant(uint32_t plantType)
{
    if (!PlantCatalog::isKnown(plantType) || plantType >= 32) return OperationResult::Invalid;
    if (isPlantUnlocked(plantType)) return OperationResult::AlreadyApplied;
    const uint32_t cost = PlantCatalog::byType(plantType).storeCost;
    if (balance_ < cost) return OperationResult::InsufficientFunds;
    const uint32_t newMask = unlockedPlantMask_ | (1u << plantType);
    if (!saveState(balance_ - cost, totalCoins_, achievementMask_, newMask, focusRewardRecordIds_))
        return OperationResult::SaveFailed;
    balance_ -= cost;
    unlockedPlantMask_ = newMask;
    emit sig_balanceChanged(balance_);
    return OperationResult::Applied;
}

bool CoinManager::hasChallengeReward(const QString& key) const
{
    return key.startsWith("challenge/") && receipts_.contains(key);
}

bool CoinManager::importChallengeClaims(const QStringList& keys)
{
    auto updated = receipts_;
    for (const auto& key : keys) {
        if (!key.startsWith("challenge/") || key.size() > 256) return false;
        if (!updated.contains(key)) updated.insert(key, {QStringLiteral("legacy-paid"), 0, 0, false});
    }
    if (updated.size() == receipts_.size()) return true;
    if (!saveState(balance_, totalCoins_, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_, &updated))
        return false;
    receipts_ = std::move(updated);
    return true;
}

CoinManager::OperationResult CoinManager::grantChallengeReward(const QString& key, uint32_t amount)
{
    if (!key.startsWith("challenge/") || key.size() > 256) return OperationResult::Invalid;
    if (hasChallengeReward(key)) return OperationResult::AlreadyApplied;
    if (amount == 0 ||
        amount > std::numeric_limits<uint32_t>::max() - balance_ ||
        amount > std::numeric_limits<uint32_t>::max() - totalCoins_) return OperationResult::Invalid;
    auto updated = receipts_;
    updated.insert(key, {QStringLiteral("reward"), 0, amount, false});
    if (!saveState(balance_ + amount, totalCoins_ + amount, achievementMask_, unlockedPlantMask_,
                   focusRewardRecordIds_, &updated)) return OperationResult::SaveFailed;
    balance_ += amount;
    totalCoins_ += amount;
    receipts_ = std::move(updated);
    emit sig_balanceChanged(balance_);
    return OperationResult::Applied;
}

bool CoinManager::hasGachaOperation(const QString& operationId) const
{
    return receipts_.contains("gacha/" + operationId);
}

CoinManager::OperationResult CoinManager::applyGachaPull(const QString& operationId, const QString& variantId,
                                                       bool isNew, uint32_t cost, uint32_t refundAmount)
{
    if (operationId.isEmpty() || operationId.size() > 128 || variantId.isEmpty() || variantId.size() > 128 ||
        cost == 0 || refundAmount > cost || (isNew && refundAmount != 0)) return OperationResult::Invalid;
    const auto key = "gacha/" + operationId;
    if (receipts_.contains(key)) {
        const auto receipt = receipts_.value(key);
        return receipt.detail == variantId && receipt.isNew == isNew && receipt.cost == cost &&
                       receipt.amount == refundAmount ? OperationResult::AlreadyApplied : OperationResult::Invalid;
    }
    if (balance_ < cost) return OperationResult::InsufficientFunds;
    const uint32_t newBalance = balance_ - cost + refundAmount;
    auto updated = receipts_;
    updated.insert(key, {variantId, cost, refundAmount, isNew});
    if (!saveState(newBalance, totalCoins_, achievementMask_, unlockedPlantMask_, focusRewardRecordIds_, &updated))
        return OperationResult::SaveFailed;
    balance_ = newBalance;
    receipts_ = std::move(updated);
    emit sig_balanceChanged(balance_);
    return OperationResult::Applied;
}
