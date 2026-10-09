#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/ChallengeManager.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/FocusSessionCoordinator.h"
#include "core/FocusSettlementManager.h"
#include "core/GuardianManager.h"
#include "core/DashboardSnapshotService.h"
#include "storage/ApplicationInstanceLock.h"
#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include <QDataStream>

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
    if (!expect(!coordinator.start(request) && database.count() == 1,
                "duplicate start is rejected without another record")) return false;
    if (!expect(settlements.isPending(0), "coordinator creates pending settlement before focus proceeds")) return false;
    coordinator.pause();
    if (!expect(controller.currentState() == FocusController::State::PAUSED, "workflow pauses an active focus")) return false;
    coordinator.resume();
    if (!expect(controller.currentState() == FocusController::State::RUNNING, "workflow resumes a paused focus")) return false;
    coordinator.abandon();
    if (!expect(terminalReceived && terminalApplied && settlements.isSettled(0),
                "coordinator routes terminal focus through durable settlement")) return false;
    terminalReceived = false;
    int startFailures = 0;
    QObject::connect(&coordinator, &FocusSessionCoordinator::startRegistrationFailed,
                     [&startFailures]() { ++startFailures; });
    StorageEnvelope::failNextWriteForTesting(QDir(directory).filePath("coordinator_settlements.dat"));
    if (!expect(!coordinator.start(request) && startFailures == 1 && !terminalReceived &&
                controller.currentState() == FocusController::State::FAILED &&
                settlements.isSettled(1),
                "failed registration stops and settles once without a second result feedback")) return false;
    return true;
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
        if (failures[index] == Failure::Challenge) {
            QByteArray oldChallenge;
            QDataStream stream(&oldChallenge, QIODevice::WriteOnly);
            stream << quint32(1) << uint32_t(1) << uint32_t(1) << uint32_t(2) << uint32_t(60)
                   << qint64(QDate::currentDate().addDays(-2).toJulianDay())
                   << qint64(QDate::currentDate().addDays(-1).toJulianDay())
                   << static_cast<uint8_t>(ChallengeStatus::PENDING);
            if (!expect(StorageEnvelope::write(challengePath, 0x46434831u, 1, oldChallenge) && challenges.load(),
                        "expired historical challenge fixture loads")) return false;
        }
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

bool testIndependentStateRollback(const QString& directory)
{
    const QString prefix = QDir(directory).filePath("rollback");
    const QString walletPath = prefix + "_coins.dat";
    const QString guardianPath = prefix + "_guardian.dat";
    DatabaseManager database((prefix + "_sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(walletPath.toStdString());
    GuardianManager guardian(guardianPath);
    if (!database.open() || !wallet.load() || !guardian.load() || !guardian.save()) return false;
    QFile originalWallet(walletPath), originalGuardian(guardianPath);
    if (!originalWallet.open(QIODevice::ReadOnly) || !originalGuardian.open(QIODevice::ReadOnly)) return false;
    const auto walletBefore = originalWallet.readAll();
    const auto guardianBefore = originalGuardian.readAll();
    originalWallet.close(); originalGuardian.close();
    FocusSettlementManager ledger(prefix + "_settlements.dat", database);
    if (!ledger.load()) return false;
    QFile ledgerFile(prefix + "_settlements.dat");
    if (!ledgerFile.open(QIODevice::ReadOnly)) return false;
    const auto ledgerBefore = ledgerFile.readAll();
    ledgerFile.close();
    FocusRecord record;
    record.status = FocusRecordStatus::Success;
    record.actualSeconds = 60;
    record.coinsEarned = 7;
    record.startTimestamp = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    const auto id = database.append(record);
    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &ledger);
    if (!ledger.markPending(id) || !service.applyTerminalRecord(id).applied) return false;
    if (!originalWallet.open(QIODevice::ReadOnly) || !originalGuardian.open(QIODevice::ReadOnly)) return false;
    const auto walletAfter = originalWallet.readAll();
    const auto guardianAfter = originalGuardian.readAll();
    originalWallet.close(); originalGuardian.close();
    bool ok = expect(ledger.confirmedRecordIds().contains(id), "new settled records keep cross-module verification credentials");
    // Even a valid envelope copied from an older time cannot silently bypass the settled ledger.
    if (!originalWallet.open(QIODevice::WriteOnly | QIODevice::Truncate) || originalWallet.write(walletBefore) != walletBefore.size()) return false;
    originalWallet.close();
    CoinManager rolledWallet; rolledWallet.setDataPath(walletPath.toStdString());
    if (!rolledWallet.load()) return false;
    AchievementEngine rolledAchievements(database, rolledWallet);
    FocusResultService walletRecovery(database, rolledWallet, rolledAchievements, nullptr, nullptr, &guardian, nullptr, &ledger);
    const auto walletOutcomes = walletRecovery.recoverPendingRecords();
    ok &= expect(walletOutcomes.size() == 1 && !walletOutcomes[0].applied && rolledWallet.balance() == 0,
                 "ambiguous settled wallet rollback stops without replaying all income");
    if (!originalGuardian.open(QIODevice::WriteOnly | QIODevice::Truncate) || originalGuardian.write(guardianBefore) != guardianBefore.size()) return false;
    originalGuardian.close();
    GuardianManager rolledGuardian(guardianPath);
    if (!rolledGuardian.load()) return false;
    FocusResultService guardianRecovery(database, wallet, achievements, nullptr, nullptr, &rolledGuardian, nullptr, &ledger);
    const auto guardianOutcomes = guardianRecovery.recoverPendingRecords();
    ok &= expect(guardianOutcomes.size() == 1 && !guardianOutcomes[0].applied && rolledGuardian.totalMinutes() == 0,
                 "settled guardian rollback stops instead of applying old time to today");
    if (!originalWallet.open(QIODevice::WriteOnly | QIODevice::Truncate) || originalWallet.write(walletAfter) != walletAfter.size()) return false;
    originalWallet.close();
    if (!originalGuardian.open(QIODevice::WriteOnly | QIODevice::Truncate) || originalGuardian.write(guardianAfter) != guardianAfter.size()) return false;
    originalGuardian.close();
    if (!ledgerFile.open(QIODevice::WriteOnly | QIODevice::Truncate) || ledgerFile.write(ledgerBefore) != ledgerBefore.size()) return false;
    ledgerFile.close();
    FocusSettlementManager rolledLedger(prefix + "_settlements.dat", database);
    if (!rolledLedger.load()) return false;
    const auto balanceBeforeRepair = wallet.balance();
    const auto minutesBeforeRepair = guardian.totalMinutes();
    FocusResultService ledgerRecovery(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &rolledLedger);
    const auto ledgerOutcomes = ledgerRecovery.recoverPendingRecords();
    ok &= expect(ledgerOutcomes.size() == 1 && ledgerOutcomes[0].applied && rolledLedger.isSettled(id) &&
                 wallet.balance() == balanceBeforeRepair && guardian.totalMinutes() == minutesBeforeRepair,
                 "provable ledger-only rollback repairs once without duplicate business effects");
    return ok;
}

}

bool testQueryFailureStopsSettlement(const QString& root)
{
    DatabaseManager database(QDir(root).filePath("query-settlement.sqlite").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(root).filePath("query-wallet.dat").toStdString());
    GuardianManager guardian(QDir(root).filePath("query-guardian.dat"));
    FocusSettlementManager ledger(QDir(root).filePath("query-ledger.dat"), database);
    ChallengeManager challenges(QDir(root).filePath("query-challenges.dat"), &database, 1);
    if (!database.open() || !wallet.load() || !guardian.load() || !ledger.load() || !challenges.load()) return false;
    FocusRecord record;
    record.status = FocusRecordStatus::Success;
    record.actualSeconds = 600;
    record.coinsEarned = 7;
    record.startTimestamp = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());
    const auto id = database.append(record);
    if (id == UINT32_MAX || !ledger.markPending(id) || !challenges.createChallenge(2, 10, QDate::currentDate())) return false;
    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, &challenges, &ledger);
    DashboardSnapshotService snapshots(database, wallet, nullptr, &guardian, &challenges);
    const auto balance = wallet.balance();
    const auto minutes = guardian.totalMinutes();
    database.close();
    const auto failed = service.recoverPendingRecords();
    bool ok = expect(failed.size() == 1 && !failed[0].applied && !failed[0].error.isEmpty(),
                     "query error produces recovery failure, not no pending work");
    ok &= expect(!service.applyTerminalRecord(id).applied && ledger.isPending(id) &&
                  wallet.balance() == balance && guardian.totalMinutes() == minutes,
                 "query error cannot settle or grant rewards");
    ok &= expect(service.evaluateAchievements().isEmpty() && !service.lastAchievementEvaluationSucceeded(),
                 "query error cannot evaluate achievements using zero history");
    ok &= expect(!snapshots.forest().error.isEmpty() && !snapshots.challenges().error.isEmpty(),
                 "page snapshots expose read failures");
    ok &= expect(!challenges.refreshProgressAfterFocus(), "challenge query failure is not successful refresh");
    FocusSettlementManager missingLedger(QDir(root).filePath("query-missing-ledger.dat"), database);
    ok &= expect(!missingLedger.load() && !QFile::exists(QDir(root).filePath("query-missing-ledger.dat")),
                 "query failure cannot initialize an empty ledger");
    if (!database.open() || !ledger.load()) return false;
    Logger::instance().setLogPath(QDir(root).filePath("uncreated-log-parent/app.log").toStdString());
    ok &= expect(!Logger::instance().open(), "diagnostic write failure is reproducible without changing data paths");
    const auto recovered = service.recoverPendingRecords();
    ok &= expect(recovered.size() == 1 && recovered[0].applied && ledger.isSettled(id),
                 "query recovery settles once even when diagnostics are unavailable");
    Logger::instance().setLogPath(QDir(root).filePath("business-events.log").toStdString());
    if (!Logger::instance().open()) return false;
    const auto settledBalance = wallet.balance();
    ok &= expect(service.applyTerminalRecord(id).alreadyApplied && wallet.balance() == settledBalance,
                 "query retry does not duplicate rewards");
    return ok;
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    if (!expect(directory.isValid(), "temporary directory is available")) return 1;
    const QString logPath = QDir(directory.path()).filePath("business-events.log");
    Logger::instance().setLogPath(logPath.toStdString());
    Logger::instance().setSessionId("business-test");
    if (!expect(Logger::instance().open(), "temporary business log opens")) return 1;
    bool ok = testQueryFailureStopsSettlement(directory.path()) &&
                    testSettlementRecovery(directory.path()) &&
                    testInterruptedFocusSettlement(directory.path()) &&
                    testSingleInstanceLock(directory.path()) &&
                    testFocusSessionCoordinator(directory.path()) &&
                    testInjectedSettlementFailures(directory.path()) &&
                    testIndependentStateRollback(directory.path());
    Logger::instance().close();
    QFile log(logPath);
    if (!expect(log.open(QIODevice::ReadOnly), "business log can be inspected")) return 1;
    const QByteArray events = log.readAll();
    ok &= expect(events.contains("[module=focus] [operation=terminal-saved]") &&
                 events.contains("[module=settlement] [operation=recover-complete]") &&
                 events.contains("[module=settlement] [operation=recover-read]") &&
                 events.contains("[module=settlement] [operation=recover-validate]") &&
                 events.contains("[module=storage] [operation=recover-interrupted]"),
                 "terminal, compensation, read failure and inconsistent recovery events are logged");
    ok &= expect(!events.contains(directory.path().toUtf8()), "business logs omit local fixture paths");
    return ok ? 0 : 1;
}
