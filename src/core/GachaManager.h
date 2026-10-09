#ifndef GACHAMANAGER_H
#define GACHAMANAGER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>
#include <optional>

class CoinManager;

class GachaManager {
public:
    struct VariantDef {
        QString id;
        QString basePlant;
        uint32_t basePlantType = 0;
        QString displayName;
        QString description;
        QString icon;
        QString imagePath;
        QString rarity;
        QString tintColor;
        bool unlocked = false;
    };

    struct PullResult {
        QString operationId;
        QString variantId;
        QString displayName;
        QString description;
        QString icon;
        QString rarity;
        QString error;
        bool success = false;
        bool isNew = false;
        int coinRefund = 0;
    };

    explicit GachaManager(const QString& filePath);
    ~GachaManager();

    bool load();
    bool save();

    const QVector<VariantDef>& allVariants() const { return m_variants; }
    QStringList unlockedVariantIds() const;
    int variantCount() const { return m_variants.size(); }
    int unlockedCount() const;

    PullResult pull(CoinManager& coins);
    std::optional<PullResult> recover(CoinManager& coins);
    QString lastError() const { return m_lastError; }
    bool isUnlocked(const QString& variantId) const;
    bool unlockVariant(const QString& variantId);

    void failVariantsSaveAfterForTesting(int successfulSaves);

    static int pullCost() { return 100; }
    static int duplicateCoinRefund() { return 50; }

private:
    enum class ProcessingState : uint8_t { AwaitingWallet = 0, WalletApplied = 1 };

    struct PendingOperation {
        bool active = false;
        QString operationId;
        QString variantId;
        bool isNew = false;
        ProcessingState state = ProcessingState::AwaitingWallet;
    };

    int indexOf(const QString& variantId) const;
    uint32_t unlockMask() const;
    void applyUnlockMask(uint32_t mask);
    bool loadVersion2(const QByteArray& payload);
    PullResult processPending(CoinManager& coins);
    PullResult resultFor(const QString& operationId, const QString& variantId, bool isNew) const;
    PullResult failedResult(const QString& error, const PendingOperation* pending = nullptr) const;
    std::optional<PullResult> consumeRecoveryResult();

    QString m_filePath;
    QVector<VariantDef> m_variants;
    uint32_t m_originalUnlockMask = 0;
    PendingOperation m_pending;
    PullResult m_lastCompletedResult;
    bool m_hasLastCompletedResult = false;
    bool m_recoveryResultAvailable = false;
    bool m_loaded = false;
    int m_variantsSavesUntilFailure = -1;
    QString m_lastError;
};

#endif
