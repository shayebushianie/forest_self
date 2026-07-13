#ifndef GACHAMANAGER_H
#define GACHAMANAGER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <cstdint>

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
        QString rarity;
        QString tintColor;
        bool unlocked = false;
    };

    struct PullResult {
        QString variantId;
        QString displayName;
        QString description;
        QString icon;
        QString rarity;
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
    bool isUnlocked(const QString& variantId) const;
    bool unlockVariant(const QString& variantId);

    static int pullCost() { return 100; }
    static int duplicateCoinRefund() { return 50; }

private:
    int indexOf(const QString& variantId) const;
    uint32_t unlockMask() const;
    void applyUnlockMask(uint32_t mask);

    QString m_filePath;
    QVector<VariantDef> m_variants;
};

#endif
