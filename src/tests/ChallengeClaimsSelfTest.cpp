#include "core/ChallengeRewardService.h"
#include "core/CoinManager.h"
#include "config/UserPreferences.h"
#include "storage/StorageEnvelope.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool expect(bool ok, const char* text)
{
    if (!ok) std::cerr << text << '\n';
    return ok;
}
bool runClaims(const QString& directory, bool inject)
{
    auto& preferences = UserPreferences::instance();
    preferences.configure(directory);
    if (!preferences.load()) return false;
    const auto path = QDir(directory).filePath("coins.dat");
    CoinManager wallet;
    wallet.setDataPath(path.toStdString());
    if (!wallet.load()) return false;
    ChallengeRewardService service(wallet);
    const QDate date = QDate::currentDate();
    FocusRecord record;
    record.status = FocusRecordStatus::Success;
    record.startTimestamp = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    record.actualSeconds = 1000 * 60;
    record.plantType = ChallengeRewardService::progress(date, {}, {}).plantType;
    const std::vector<FocusRecord> records{record};
    const auto data = ChallengeRewardService::progress(date, {}, records);
    bool ok = true;
    using RewardState = ChallengeRewardService::RewardState;
    const auto initial = service.snapshot(date, {}, records);
    ok &= expect(initial.date == date && initial.error.isEmpty() &&
                 initial.checkins.size() == data.checkins.size() && initial.tasks.size() == data.tasks.size(),
                 "snapshot uses the service reward definitions");
    auto rewardState = [&](ChallengeRewardService::Kind kind, int index) {
        const auto snapshot = service.snapshot(date, {}, records);
        if (kind == ChallengeRewardService::Kind::Checkin) return snapshot.checkins[index].state;
        if (kind == ChallengeRewardService::Kind::Daily) return snapshot.tasks[index].state;
        return snapshot.monthlyState;
    };
    const QVector<ChallengeRewardService::Kind> kinds = {ChallengeRewardService::Kind::Checkin,
        ChallengeRewardService::Kind::Daily, ChallengeRewardService::Kind::Monthly};
    for (auto kind : kinds) {
        const int first = kind == ChallengeRewardService::Kind::Checkin ? data.currentCheckin : 0;
        const int count = kind == ChallengeRewardService::Kind::Daily ? data.tasks.size() : 1;
        for (int i = first; i < first + count; ++i) {
            const auto before = wallet.balance();
            const auto wasClaimed = service.isClaimed(kind, i, date);
            ok &= expect(rewardState(kind, i) == (wasClaimed ? RewardState::Claimed : RewardState::Available),
                         "snapshot reads claim eligibility and durable receipts");
            if (inject && !wasClaimed) {
                StorageEnvelope::failNextWriteForTesting(path);
                const auto failed = service.claim(kind, i, date, {}, records);
                ok &= expect(failed.status == ChallengeRewardService::Status::SaveFailed &&
                             wallet.balance() == before && !service.isClaimed(kind, i, date) &&
                             rewardState(kind, i) == RewardState::Available,
                             "claim write failure cannot mark paid or increment balance");
            }
            const auto result = service.claim(kind, i, date, {}, records);
            ok &= expect(result.status == (wasClaimed ? ChallengeRewardService::Status::AlreadyClaimed : ChallengeRewardService::Status::Applied),
                         "each reward has one durable result");
            const auto after = wallet.balance();
            ok &= expect(rewardState(kind, i) == RewardState::Claimed, "snapshot immediately reflects successful claim");
            ok &= expect(service.claim(kind, i, date, {}, records).status == ChallengeRewardService::Status::AlreadyClaimed &&
                         wallet.balance() == after, "continuous click does not issue duplicate reward");
        }
    }
    int expected = data.checkins[data.currentCheckin].coins + data.monthlyCoins;
    for (const auto& task : data.tasks) expected += task.coins;
    ok &= expect(wallet.balance() == static_cast<uint32_t>(expected), "checkin/daily/monthly use one reward schedule");
    ok &= expect(service.claim(ChallengeRewardService::Kind::Checkin, (data.currentCheckin + 1) % data.checkins.size(), date, {}, records).status ==
                 ChallengeRewardService::Status::NotReady, "future or expired reward is not payable");
    ok &= expect(service.claim(ChallengeRewardService::Kind::Daily, 0, date.addDays(-1), {}, records).status ==
                 ChallengeRewardService::Status::NotReady, "old period does not mint another reward");
    const auto future = service.snapshot(date.addDays(1), {}, records);
    ok &= expect(future.checkins[future.currentCheckin].state == RewardState::Locked &&
                 future.tasks[0].state == RewardState::NotReady,
                 "future period is not displayed as claimable");
    const auto invalid = service.snapshot({}, {}, records);
    ok &= expect(!invalid.error.isEmpty() && invalid.checkins.isEmpty() && invalid.tasks.isEmpty() &&
                 invalid.monthlyState == RewardState::Unavailable,
                 "invalid reward date returns an unavailable snapshot");
    ok &= expect(service.claim(ChallengeRewardService::Kind::Daily, data.tasks.size(), date, {}, records).status ==
                 ChallengeRewardService::Status::NotReady, "invalid task index cannot issue rewards");
    return ok;
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--resume")
        return runClaims(app.arguments()[2], false) ? 0 : 1;
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const QString first = QDir(temporary.path()).filePath("user_1");
    if (!runClaims(first, true)) return 1;
    QProcess child;
    child.start(app.applicationFilePath(), {"--resume", first});
    if (!expect(child.waitForFinished(20000) && child.exitCode() == 0, "restart retains all reward credentials")) return 1;
    const QString second = QDir(temporary.path()).filePath("user_2");
    auto& preferences = UserPreferences::instance();
    preferences.configure(second);
    CoinManager wallet;
    wallet.setDataPath(QDir(second).filePath("coins.dat").toStdString());
    if (!preferences.load() || !wallet.load()) return 1;
    ChallengeRewardService service(wallet);
    bool ok = expect(wallet.balance() == 0 && !service.isClaimed(ChallengeRewardService::Kind::Monthly, 0, QDate::currentDate()),
                     "second account shares neither balance nor receipts");
    ok &= expect(service.claim(ChallengeRewardService::Kind::Daily, 1, QDate::currentDate(), {}, {}).status ==
                 ChallengeRewardService::Status::NotReady, "incomplete task cannot issue rewards");
    ok &= expect(service.snapshot(QDate::currentDate(), {}, {}).tasks[1].state ==
                 ChallengeRewardService::RewardState::NotReady, "snapshot identifies incomplete task");
    auto old = preferences.challengeRewardState(QDate::currentDate(), 4, 5);
    old.taskClaimed[1] = true;
    if (!preferences.saveChallengeRewardState(old)) return 1;
    ok &= expect(service.synchronizeLegacy() && service.isClaimed(ChallengeRewardService::Kind::Daily, 1, QDate::currentDate()) &&
                 wallet.balance() == 0 && service.snapshot(QDate::currentDate(), {}, {}).tasks[1].state ==
                 ChallengeRewardService::RewardState::Claimed, "owned legacy slot maps to stable ID without regranting");
    return ok ? 0 : 1;
}
