#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/ChallengeManager.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/FocusSessionCoordinator.h"
#include "core/FocusSettlementManager.h"
#include "core/GuardianManager.h"
#include "storage/ApplicationInstanceLock.h"
#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << "\n";
    return condition;
}

bool testSettlementRecovery(const QString& directory)
{
    const QString sessionsPath = QDir(directory).filePath("sessions.dat");
    const QString walletPath = QDir(directory).filePath("coins.dat");
    const QString guardianPath = QDir(directory).filePath("guardian.dat");
    const QString settlementsPath = QDir(directory).filePath("focus_settlements.dat");

    DatabaseManager database(sessionsPath.toStdString());
    CoinManager wallet;
    wallet.setDataPath(walletPath.toStdString());
    GuardianManager guardian(guardianPath);
    if (!expect(database.open() && wallet.load() && guardian.load(), "settlement dependencies load")) return false;

    FocusSettlementManager settlements(settlementsPath, database);
    if (!expect(settlements.load(), "settlement ledger initializes")) return false;

    FocusRecord record;
    record.status = FocusRecordStatus::Success;
    record.actualSeconds = 10 * 60;
    record.coinsEarned = 7;
    record.startTimestamp = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    const uint32_t recordId = database.append(record);
    if (!expect(recordId != UINT32_MAX && settlements.markPending(recordId),
                "terminal focus is registered as pending before settlement")) return false;

    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &settlements);
    const QVector<FocusResultService::Outcome> recovered = service.recoverPendingRecords();
    if (!expect(recovered.size() == 1 && recovered.first().applied,
                "pending terminal focus is recovered")) return false;
    if (!expect(!settlements.isPending(recordId) && wallet.balance() >= 7 && guardian.totalMinutes() == 10,
                "recovery applies reward and guardian minutes once")) return false;

    const uint32_t balanceAfterRecovery = wallet.balance();
    const uint32_t minutesAfterRecovery = guardian.totalMinutes();
    FocusResultService retry(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &settlements);
    return expect(retry.recoverPendingRecords().isEmpty(), "settled focus is not replayed") &&
           expect(wallet.balance() == balanceAfterRecovery && guardian.totalMinutes() == minutesAfterRecovery,
                  "settlement recovery remains idempotent");
}

bool testInterruptedFocusSettlement(const QString& directory)
{
    const QString sessionsPath = QDir(directory).filePath("interrupted_sessions.dat");
    const QString settlementsPath = QDir(directory).filePath("interrupted_settlements.dat");
    {
        DatabaseManager database(sessionsPath.toStdString());
        if (!expect(database.open(), "interrupted database opens")) return false;
        FocusSettlementManager settlements(settlementsPath, database);
        if (!expect(settlements.load(), "interrupted settlement ledger initializes")) return false;
        FocusRecord running;
        running.status = FocusRecordStatus::Running;
        running.actualSeconds = 30;
        const uint32_t recordId = database.append(running);
        if (!expect(recordId != UINT32_MAX && settlements.markPending(recordId),
                    "running focus is recoverable through settlement ledger")) return false;
    }

    DatabaseManager reopened(sessionsPath.toStdString());
    if (!expect(reopened.open(), "interrupted database reopens")) return false;
    FocusSettlementManager settlements(settlementsPath, reopened);
    if (!expect(settlements.load(), "interrupted settlement ledger reloads")) return false;
    CoinManager wallet;
    wallet.setDataPath(QDir(directory).filePath("interrupted_coins.dat").toStdString());
    GuardianManager guardian(QDir(directory).filePath("interrupted_guardian.dat"));
    if (!expect(wallet.load() && guardian.load(), "interrupted settlement dependencies load")) return false;
    AchievementEngine achievements(reopened, wallet);
    FocusResultService service(reopened, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &settlements);
    const QVector<FocusResultService::Outcome> outcomes = service.recoverPendingRecords();
    const auto record = reopened.readById(0);
    return expect(record.has_value() && record->status == FocusRecordStatus::Abandoned, "interrupted record becomes abandoned") &&
           expect(outcomes.size() == 1 && outcomes.first().applied && settlements.pendingRecordIds().isEmpty(),
                  "abandoned interrupted focus settles without a pending leak") &&
           expect(wallet.balance() == 0 && guardian.totalMinutes() == 0,
                  "abandoned interrupted focus receives no success rewards");
}

bool testSingleInstanceLock(const QString& directory)
{
    ApplicationInstanceLock first(directory);
    ApplicationInstanceLock second(directory);
    QString error;
    return expect(first.acquire(), "first application instance acquires lock") &&
           expect(!second.acquire(&error) && !error.isEmpty(), "second application instance is blocked");
}

bool testFocusSessionCoordinator(const QString& directory)
{
    DatabaseManager database(QDir(directory).filePath("coordinator_sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(directory).filePath("coordinator_coins.dat").toStdString());
    if (!expect(database.open() && wallet.load(), "coordinator dependencies load")) return false;

    FocusSettlementManager settlements(QDir(directory).filePath("coordinator_settlements.dat"), database);
    if (!expect(settlements.load(), "coordinator settlement ledger initializes")) return false;
    AchievementEngine achievements(database, wallet);
    FocusResultService results(database, wallet, achievements, nullptr, nullptr, nullptr, nullptr, &settlements);
    FocusController controller(database);
    FocusSessionCoordinator coordinator(controller, results);

    bool terminalReceived = false;
    bool terminalApplied = false;
    QObject::connect(&coordinator, &FocusSessionCoordinator::terminalResultReady,
                     [&terminalReceived, &terminalApplied](uint32_t, uint32_t,
                                                           const FocusResultService::Outcome& outcome) {
                         terminalReceived = true;
                         terminalApplied = outcome.applied;
                     });
    FocusSessionCoordinator::StartRequest request;
    request.plannedMinutes = 10;
    request.allowPause = true;
    if (!expect(coordinator.start(request), "coordinator starts a durably registered focus")) return false;
    if (!expect(settlements.isPending(0), "coordinator creates pending settlement before focus proceeds")) return false;
    coordinator.pause();
    if (!expect(controller.currentState() == FocusController::State::PAUSED, "workflow pauses an active focus")) return false;
    coordinator.resume();
    if (!expect(controller.currentState() == FocusController::State::RUNNING, "workflow resumes a paused focus")) return false;
    coordinator.abandon();
    return expect(terminalReceived && terminalApplied && settlements.isSettled(0),
                  "coordinator routes terminal focus through durable settlement");
}

bool testInjectedSettlementFailures(const QString& directory)
{
    enum class Failure { Wallet, Guardian, Challenge, Achievement, Settlement };
    const QVector<Failure> failures = {Failure::Wallet, Failure::Guardian, Failure::Challenge,
                                       Failure::Achievement, Failure::Settlement};
    for (int index = 0; index < failures.size(); ++index) {
        const QString prefix = QDir(directory).filePath(QStringLiteral("failure_%1").arg(index));
        const QString sessionsPath = prefix + "_sessions.dat";
        const QString walletPath = prefix + "_coins.dat";
        const QString guardianPath = prefix + "_guardian.dat";
        const QString challengePath = prefix + "_challenges.dat";
        const QString settlementPath = prefix + "_settlements.dat";
        DatabaseManager database(sessionsPath.toStdString());
        CoinManager wallet; wallet.setDataPath(walletPath.toStdString());
        GuardianManager guardian(guardianPath);
        ChallengeManager challenges(challengePath, &database, 1);
        if (!expect(database.open() && wallet.load() && guardian.load() && challenges.load(), "failure dependencies load")) return false;
        FocusSettlementManager settlements(settlementPath, database);
        if (!expect(settlements.load(), "failure settlement ledger initializes")) return false;
        if (failures[index] == Failure::Challenge &&
            !expect(challenges.createChallenge(2, 60, QDate::currentDate().addDays(-1)), "expired challenge is persisted")) return false;
        FocusRecord record; record.status = FocusRecordStatus::Success; record.actualSeconds = 60;
        record.coinsEarned = failures[index] == Failure::Achievement ? 0 : 7;
        record.startTimestamp = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
        const uint32_t recordId = database.append(record);
        if (!expect(recordId != UINT32_MAX && settlements.markPending(recordId), "failure record is pending")) return false;
        QString failedPath;
        switch (failures[index]) {
        case Failure::Wallet: case Failure::Achievement: failedPath = walletPath; break;
        case Failure::Guardian: failedPath = guardianPath; break;
        case Failure::Challenge: failedPath = challengePath; break;
        case Failure::Settlement: failedPath = settlementPath; break;
        }
        StorageEnvelope::failNextWriteForTesting(failedPath);
        AchievementEngine achievements(database, wallet);
        FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, &challenges, &settlements);
        const auto failed = service.applyTerminalRecord(recordId);
        if (!expect(!failed.applied && settlements.isPending(recordId), "injected failure retains pending settlement")) return false;
        StorageEnvelope::clearWriteFailuresForTesting();

        DatabaseManager reopened(sessionsPath.toStdString());
        CoinManager reloadedWallet; reloadedWallet.setDataPath(walletPath.toStdString());
        GuardianManager reloadedGuardian(guardianPath);
        ChallengeManager reloadedChallenges(challengePath, &reopened, 1);
        if (!expect(reopened.open() && reloadedWallet.load() && reloadedGuardian.load() && reloadedChallenges.load(), "restart reloads failed settlement state")) return false;
        FocusSettlementManager reloadedSettlements(settlementPath, reopened);
        if (!expect(reloadedSettlements.load(), "restart reloads settlement ledger")) return false;
        AchievementEngine reloadedAchievements(reopened, reloadedWallet);
        FocusResultService recovery(reopened, reloadedWallet, reloadedAchievements, nullptr, nullptr,
                                    &reloadedGuardian, &reloadedChallenges, &reloadedSettlements);
        const auto recovered = recovery.recoverPendingRecords();
        if (!expect(recovered.size() == 1 && recovered.first().applied && reloadedSettlements.isSettled(recordId),
                    "restart compensates injected failure exactly once")) return false;
    }
    return true;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!expect(directory.isValid(), "temporary directory is available")) return 1;
    const bool ok = testSettlementRecovery(directory.path()) &&
                    testInterruptedFocusSettlement(directory.path()) &&
                    testSingleInstanceLock(directory.path()) &&
                    testFocusSessionCoordinator(directory.path()) &&
                    testInjectedSettlementFailures(directory.path());
    return ok ? 0 : 1;
}
