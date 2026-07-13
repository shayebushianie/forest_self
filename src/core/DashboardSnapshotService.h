#ifndef DASHBOARD_SNAPSHOT_SERVICE_H
#define DASHBOARD_SNAPSHOT_SERVICE_H

#include <QMap>
#include <QVector>
#include <QString>
#include <cstdint>
#include <vector>

#include "common/DatabaseCommon.h"
#include "core/ChallengeManager.h"

class CoinManager;
class DatabaseManager;
class GuardianManager;
class TagManager;

// Read-only query facade for dashboard pages. It keeps aggregation out of QWidget code.
class DashboardSnapshotService final {
public:
    struct ForestSnapshot {
        std::vector<FocusRecord> records;
        QMap<uint32_t, QString> tagNames;
    };

    struct ChallengeSnapshot {
        uint32_t coinBalance = 0;
        QVector<Challenge> challenges;
        std::vector<FocusRecord> records;
    };

    struct GuardianSnapshot {
        uint32_t todayMinutes = 0;
        uint32_t dailyGoalMinutes = 0;
        uint32_t currentStreak = 0;
        uint32_t longestStreak = 0;
        uint32_t totalMinutes = 0;
    };

    DashboardSnapshotService(DatabaseManager& database, CoinManager& coins,
                             TagManager* tags, GuardianManager* guardian,
                             ChallengeManager* challenges);

    ForestSnapshot forest() const;
    ChallengeSnapshot challenges() const;
    GuardianSnapshot guardian();

private:
    DatabaseManager& database_;
    CoinManager& coins_;
    TagManager* tags_ = nullptr;
    GuardianManager* guardian_ = nullptr;
    ChallengeManager* challenges_ = nullptr;
};

#endif // DASHBOARD_SNAPSHOT_SERVICE_H
