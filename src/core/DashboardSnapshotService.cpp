#include "core/DashboardSnapshotService.h"

#include "core/CoinManager.h"
#include "core/GuardianManager.h"
#include "core/TagManager.h"
#include "storage/DatabaseManager.h"
#include <QSet>

DashboardSnapshotService::DashboardSnapshotService(DatabaseManager& database, CoinManager& coins,
                                                   TagManager* tags, GuardianManager* guardian,
                                                   ChallengeManager* challenges)
    : database_(database), coins_(coins), tags_(tags), guardian_(guardian), challenges_(challenges)
{
}

DashboardSnapshotService::ForestSnapshot DashboardSnapshotService::forest() const
{
    ForestSnapshot snapshot;
    if (!database_.getAllRecords(snapshot.records)) {
        snapshot.error = database_.lastError();
        return snapshot;
    }
    if (tags_) {
        for (const TagDef& tag : tags_->all()) {
            snapshot.tagNames.insert(tag.id, tag.name);
        }
    }
    return snapshot;
}

DashboardSnapshotService::ChallengeSnapshot DashboardSnapshotService::challenges() const
{
    ChallengeSnapshot snapshot;
    snapshot.coinBalance = coins_.balance();
    if (!database_.getAllRecords(snapshot.records)) {
        snapshot.error = database_.lastError();
        return snapshot;
    }
    if (!challenges_) return snapshot;

    QSet<uint32_t> seen;
    QVector<Challenge> active;
    if (!challenges_->active(active)) {
        snapshot.error = database_.lastError();
        if (snapshot.error.isEmpty()) snapshot.error = QStringLiteral("无法读取本地挑战进度。");
        return snapshot;
    }
    for (const Challenge& challenge : active) {
        if (seen.contains(challenge.id)) continue;
        seen.insert(challenge.id);
        snapshot.challenges.append(challenge);
    }
    return snapshot;
}

DashboardSnapshotService::GuardianSnapshot DashboardSnapshotService::guardian()
{
    GuardianSnapshot snapshot;
    if (!guardian_) return snapshot;
    guardian_->checkDayBoundary();
    snapshot.todayMinutes = guardian_->todayMinutes();
    snapshot.dailyGoalMinutes = guardian_->dailyGoalMinutes();
    snapshot.currentStreak = guardian_->currentStreak();
    snapshot.longestStreak = guardian_->longestStreak();
    snapshot.totalMinutes = guardian_->totalMinutes();
    return snapshot;
}
