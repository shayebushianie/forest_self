#ifndef ACHIEVEMENTENGINE_H
#define ACHIEVEMENTENGINE_H

#include <QObject>
#include <QVector>
#include <QString>
#include "storage/DatabaseManager.h"
#include "core/CoinManager.h"

// Evaluates focus history, tracks achievement definitions, and persists unlock state.
class AchievementEngine : public QObject {
    Q_OBJECT

public:
    struct Info {
        QString id;
        QString name;
        QString description;
        QString icon;
        QString rarity;     // "gold" / "silver" / "common"
        uint32_t rewardCoins = 0;
        uint8_t storageBit = 0;
        bool unlocked = false;
    };

    struct Context {
        int totalSessions = 0;
        int totalCoinsEarned = 0;
        int totalSuccessCount = 0;
        int totalFailCount = 0;
        int totalSeconds = 0;
        bool lastSessionSuccess = false;
        QString lastPlantType;
        int lastSessionDuration = 0;
        int lastSessionViolations = 0;
        bool lastSessionStrict = false;
        int unlockedPlantCount = 0;
        int forestPlacementCount = 0;
        int variantCount = 0;
        bool plantedOak = false;
        bool plantedPine = false;
        bool plantedRose = false;
        bool plantedGinkgo = false;
        bool plantedSunflower = false;
        bool plantedCactus = false;
    };

    AchievementEngine(DatabaseManager& db, CoinManager& coinMgr, QObject* parent = nullptr);

    const QVector<Info>& all() const { return m_infos; }
    bool isUnlocked(int index) const;
    int count() const { return m_infos.size(); }
    bool lastCheckSucceeded() const { return lastCheckSucceeded_; }

    QVector<int> checkAll(const Context& ctx);

signals:
    void sig_achievementUnlocked(uint32_t id, const QString& title, const QString& desc);

private:
    void setupDefs();
    void validateDefinitions() const;
    void loadStateFromMask();
    bool unlock(int index);
    void unlockById(const QString& id, bool condition, QVector<int>& newlyUnlocked);
    int indexOf(const QString& id) const;

    DatabaseManager& db_;
    CoinManager& coinMgr_;
    QVector<Info> m_infos;
    bool lastCheckSucceeded_ = true;
};

#endif // ACHIEVEMENTENGINE_H
