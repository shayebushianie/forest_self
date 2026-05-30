#ifndef ACHIEVEMENTENGINE_H
#define ACHIEVEMENTENGINE_H

#include <QObject>
#include "storage/DatabaseManager.h"
#include "core/CoinManager.h"

class AchievementEngine : public QObject {
    Q_OBJECT

public:
    AchievementEngine(DatabaseManager& db, CoinManager& coinMgr, QObject* parent = nullptr)
        : QObject(parent), db_(db), coinMgr_(coinMgr) {}

    void checkAndUnlock();

signals:
    void sig_achievementUnlocked(uint32_t id, const QString& title, const QString& desc);

private:
    void unlock(uint32_t bit, const QString& title, const QString& desc);

    DatabaseManager& db_;
    CoinManager& coinMgr_;
};

#endif // ACHIEVEMENTENGINE_H
