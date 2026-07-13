#include "core/DashboardSnapshotService.h"

#include "core/CoinManager.h"
#include "core/GuardianManager.h"
#include "core/TagManager.h"
#include "storage/DatabaseManager.h"

DashboardSnapshotService::DashboardSnapshotService(DatabaseManager& database, CoinManager& coins,
                                                   TagManager* tags, GuardianManager* guardian,
                                                   ChallengeManager* challenges)
    : database_(database), coins_(coins), tags_(tags), guardian_(guardian), challenges_(challenges)
{
}

DashboardSnapshotService::ForestSnapshot DashboardSnapshotService::forest() const
{
    ForestSnapshot snapshot;
    snapshot.records = database_.getAllRecords();
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
    snapshot.records = database_.getAllRecords();
    if (!challenges_) return snapshot;

    snapshot.challenges = challenges_->active();
    for (const Challenge& challenge : challenges_->incoming()) {
        if (challenge.status != ChallengeStatus::PENDING) continue;
        Challenge pending = challenge;
        challenges_->recalculateProgress(pending);
        snapshot.challenges.append(pending);
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
