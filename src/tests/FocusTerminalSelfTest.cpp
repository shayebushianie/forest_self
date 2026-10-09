#include "core/FocusController.h"
#include "storage/DatabaseManager.h"
#include "core/CoinManager.h"
#include "core/FocusResultService.h"
#include "core/FocusSettlementManager.h"
#include "core/GuardianManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdint>
#include <iostream>
#include <limits>

namespace {

struct TerminalDecision {
    FocusRecordStatus status = FocusRecordStatus::Abandoned;
    uint32_t actualSeconds = 0;
    uint32_t violationCount = 0;
    uint32_t growthStage = 0;
    uint32_t coinsEarned = 0;
};

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

FocusRecord makeRunningRecord()
{
    FocusRecord record;
    record.plantType = 2;
    record.plannedMinutes = 45;
    record.actualSeconds = 17;
    record.startTimestamp = 1700000000;
    record.status = FocusRecordStatus::Running;
    record.violationCount = 1;
    record.growthStage = 1;
    record.coinsEarned = 0;
    record.tagId = 23;
    record.focusMode = PersistedFocusMode::Deep;
    record.gridIndex = 9;
    return record;
}

FocusRecord withDecision(FocusRecord record, uint32_t recordId, const TerminalDecision& decision)
{
    record.recordId = recordId;
    record.status = decision.status;
    record.actualSeconds = decision.actualSeconds;
    record.violationCount = decision.violationCount;
    record.growthStage = decision.growthStage;
    record.coinsEarned = decision.coinsEarned;
    return record;
}

bool matchesDecision(const FocusRecord& record, const TerminalDecision& decision)
{
    return record.status == decision.status &&
           record.actualSeconds == decision.actualSeconds &&
           record.violationCount == decision.violationCount &&
           record.growthStage == decision.growthStage &&
           record.coinsEarned == decision.coinsEarned;
}

bool sameDecision(const TerminalDecision& left, const TerminalDecision& right)
{
    return left.status == right.status &&
           left.actualSeconds == right.actualSeconds &&
           left.violationCount == right.violationCount &&
           left.growthStage == right.growthStage &&
           left.coinsEarned == right.coinsEarned;
}

bool matchesStoredRecord(const FocusRecord& actual,
                         uint32_t recordId,
                         const TerminalDecision& decision)
{
    const FocusRecord original = makeRunningRecord();
    return actual.recordId == recordId &&
           actual.plantType == original.plantType &&
           actual.plannedMinutes == original.plannedMinutes &&
           actual.startTimestamp == original.startTimestamp &&
           actual.tagId == original.tagId &&
           actual.focusMode == original.focusMode &&
           actual.gridIndex == original.gridIndex &&
           matchesDecision(actual, decision);
}

QString databasePath(const QString& root, const QString& name)
{
    return QDir(root).filePath(name + QStringLiteral(".sqlite"));
}

bool queryInteger(const QString& path, const QString& sql, qlonglong* value)
{
    const QString connectionName =
        QStringLiteral("terminal_test_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase connection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    connection.setDatabaseName(path);

    bool ok = connection.open();
    if (ok) {
        QSqlQuery query(connection);
        ok = query.exec(sql) && query.next();
        if (ok) *value = query.value(0).toLongLong();
        query.finish();
    }

    connection.close();
    connection = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool readIntent(const QString& path, uint32_t recordId, TerminalDecision* decision)
{
    const QString connectionName =
        QStringLiteral("terminal_intent_test_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase connection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    connection.setDatabaseName(path);

    bool ok = connection.open();
    if (ok) {
        QSqlQuery query(connection);
        query.prepare(QStringLiteral(
            "SELECT actual_seconds,status,violation_count,growth_stage,coins_earned "
            "FROM focus_terminal_intents WHERE record_id=?"));
        query.addBindValue(recordId);
        ok = query.exec() && query.next();
        if (ok) {
            decision->actualSeconds = query.value(0).toUInt();
            decision->status = static_cast<FocusRecordStatus>(query.value(1).toUInt());
            decision->violationCount = query.value(2).toUInt();
            decision->growthStage = query.value(3).toUInt();
            decision->coinsEarned = query.value(4).toUInt();
        }
        query.finish();
    }

    connection.close();
    connection = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool createVersionFixture(const QString& path, int version, bool includeV2Record)
{
    const QString connectionName =
        QStringLiteral("terminal_schema_test_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase connection = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    connection.setDatabaseName(path);

    bool ok = connection.open();
    if (ok) {
        const auto execute = [&connection](const QString& sql) {
            QSqlQuery query(connection);
            const bool executed = query.exec(sql);
            if (!executed) std::cerr << query.lastError().text().toStdString() << '\n';
            return executed;
        };

        ok = execute(QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)"));
        if (ok) {
            QSqlQuery insertVersion(connection);
            insertVersion.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(?)"));
            insertVersion.addBindValue(version);
            ok = insertVersion.exec();
        }

        if (ok && includeV2Record) {
            ok = execute(QStringLiteral(
                "CREATE TABLE focus_records ("
                "record_id INTEGER PRIMARY KEY, plant_type INTEGER NOT NULL, planned_minutes INTEGER NOT NULL, "
                "actual_seconds INTEGER NOT NULL, start_timestamp INTEGER NOT NULL, status INTEGER NOT NULL, "
                "violation_count INTEGER NOT NULL, growth_stage INTEGER NOT NULL, coins_earned INTEGER NOT NULL, "
                "tag_id INTEGER NOT NULL, focus_mode INTEGER NOT NULL, grid_index INTEGER NOT NULL)"));
            if (ok) {
                ok = execute(QStringLiteral(
                    "INSERT INTO focus_records VALUES(7,2,45,777,1700000000,0,0,2,2,23,0,9)"));
            }
        }
    }

    connection.close();
    connection = QSqlDatabase();
    QSqlDatabase::removeDatabase(connectionName);
    return ok;
}

bool recoverInChildAndVerify(const QString& path,
                             uint32_t recordId,
                             const TerminalDecision& decision,
                             bool settle = false)
{
    QStringList arguments{
        settle ? QStringLiteral("--recover-and-settle") : QStringLiteral("--recover"),
        path,
        QString::number(recordId),
        QString::number(static_cast<uint32_t>(decision.status)),
        QString::number(decision.actualSeconds),
        QString::number(decision.violationCount),
        QString::number(decision.growthStage),
        QString::number(decision.coinsEarned)
    };

    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), arguments);
    if (!child.waitForStarted(5000)) {
        std::cerr << child.errorString().toStdString() << '\n';
        return expect(false, "recovery child starts");
    }
    if (!child.waitForFinished(15000)) {
        child.kill();
        child.waitForFinished();
        return expect(false, "recovery child finishes");
    }
    if (!expect(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                "recovery child opens and verifies the terminal record")) {
        std::cerr << child.readAllStandardOutput().toStdString()
                  << child.readAllStandardError().toStdString();
        return false;
    }

    DatabaseManager reopened(path.toStdString());
    if (!expect(reopened.open(), "database reopens after child recovery")) {
        std::cerr << reopened.lastError().toStdString() << '\n';
        return false;
    }
    const auto record = reopened.readById(recordId);
    const bool ok = expect(reopened.count() == 1, "recovery keeps exactly one history row") &&
                    expect(record.has_value() &&
                           matchesStoredRecord(*record, recordId, decision),
                           "reopen preserves the exact terminal fields");
    reopened.close();
    return ok;
}

bool runRecoveryChild(const QStringList& arguments)
{
    if (arguments.size() != 9) return expect(false, "recovery child arguments are valid");

    bool idOk = false;
    bool statusOk = false;
    bool secondsOk = false;
    bool violationsOk = false;
    bool growthOk = false;
    bool coinsOk = false;
    const uint32_t recordId = arguments[3].toUInt(&idOk);
    TerminalDecision decision;
    decision.status = static_cast<FocusRecordStatus>(arguments[4].toUInt(&statusOk));
    decision.actualSeconds = arguments[5].toUInt(&secondsOk);
    decision.violationCount = arguments[6].toUInt(&violationsOk);
    decision.growthStage = arguments[7].toUInt(&growthOk);
    decision.coinsEarned = arguments[8].toUInt(&coinsOk);
    if (!idOk || !statusOk || !secondsOk || !violationsOk || !growthOk || !coinsOk) {
        return expect(false, "recovery child values parse");
    }

    DatabaseManager database(arguments[2].toStdString());
    if (!expect(database.open(), "child open recovers durable terminal intent")) {
        std::cerr << database.lastError().toStdString() << '\n';
        return false;
    }
    const auto record = database.readById(recordId);
    if (!expect(database.count() == 1, "child sees one focus history row") ||
        !expect(record.has_value() && matchesStoredRecord(*record, recordId, decision),
                "child sees the original final status, seconds, growth, and reward")) return false;
    if (arguments[1] != QStringLiteral("--recover-and-settle")) return true;

    const QString prefix = arguments[2];
    CoinManager wallet;
    wallet.setDataPath((prefix + QStringLiteral(".coins")).toStdString());
    GuardianManager guardian(prefix + QStringLiteral(".guardian"));
    FocusSettlementManager settlements(prefix + QStringLiteral(".settlements"), database);
    if (!expect(wallet.load() && guardian.load() && settlements.load(),
                "child loads the complete pending settlement state")) return false;
    const bool pending = settlements.isPending(recordId);
    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian,
                               nullptr, &settlements);
    const auto outcomes = service.recoverPendingRecords();
    return expect(pending ? outcomes.size() == 1 && outcomes[0].applied : outcomes.isEmpty(),
                  "child compensates once and subsequent restart has no work") &&
           expect(settlements.isSettled(recordId) && !settlements.isPending(recordId) &&
                  wallet.hasFocusReward(recordId) && guardian.hasCompletedFocus(recordId),
                  "child confirms ledger, wallet and guardian receipts") &&
           expect(guardian.totalMinutes() == decision.actualSeconds / 60,
                  "child restores the exact completed focus duration");
}

bool testTerminalAndSettlementRestart(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("terminal_and_settlement"));
    const TerminalDecision decision{FocusRecordStatus::Success, 1500, 0, 3, 5};
    uint32_t recordId = 0;
    {
        DatabaseManager database(path.toStdString());
        CoinManager wallet;
        wallet.setDataPath((path + QStringLiteral(".coins")).toStdString());
        GuardianManager guardian(path + QStringLiteral(".guardian"));
        FocusSettlementManager settlements(path + QStringLiteral(".settlements"), database);
        if (!expect(database.open() && wallet.load() && guardian.load() && settlements.load(),
                    "combined restart fixture loads before focus")) return false;
        recordId = database.append(makeRunningRecord());
        if (!expect(recordId != UINT32_MAX && settlements.markPending(recordId),
                    "combined restart registers the active record")) return false;
        database.failNextTerminalApplyForTesting();
        if (!expect(!database.finalizeRecord(recordId, withDecision(makeRunningRecord(), recordId, decision)),
                    "combined restart interrupts after durable terminal intent")) return false;
    }
    if (!recoverInChildAndVerify(path, recordId, decision, true)) return false;
    CoinManager firstWallet;
    firstWallet.setDataPath((path + QStringLiteral(".coins")).toStdString());
    if (!expect(firstWallet.load() && firstWallet.balance() >= decision.coinsEarned,
                "first restart durably credits focus and achievement rewards")) return false;
    const uint32_t balance = firstWallet.balance();
    const uint32_t achievements = firstWallet.getUnlockedAchievementMask();
    if (!recoverInChildAndVerify(path, recordId, decision, true)) return false;
    CoinManager secondWallet;
    secondWallet.setDataPath((path + QStringLiteral(".coins")).toStdString());
    return expect(secondWallet.load() && secondWallet.balance() == balance &&
                  secondWallet.getUnlockedAchievementMask() == achievements,
                  "second process restart never duplicates rewards or achievements");
}

bool testApplyFailureRestart(const QString& root,
                             const QString& name,
                             const TerminalDecision& decision)
{
    const QString path = databasePath(root, name);
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "apply-failure database opens")) return false;

    FocusRecord original = makeRunningRecord();
    const uint32_t recordId = database.append(original);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max(),
                "running record is inserted before terminal decision")) return false;
    original.recordId = recordId;
    const FocusRecord terminal = withDecision(original, recordId, decision);

    database.failNextTerminalApplyForTesting();
    const bool failedAfterIntent = !database.finalizeRecord(recordId, terminal);
    const auto beforeRestart = database.readById(recordId);
    const bool remainedRunning = beforeRestart.has_value() &&
                                 beforeRestart->status == FocusRecordStatus::Running;
    database.close();

    if (!expect(failedAfterIntent, "injected apply failure is reported")) return false;
    if (!expect(remainedRunning, "durable intent leaves the history row pending until recovery")) return false;
    return recoverInChildAndVerify(path, recordId, decision);
}

bool testRetryIdempotence(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("retry_idempotence"));
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "retry database opens")) return false;

    FocusRecord original = makeRunningRecord();
    const uint32_t recordId = database.append(original);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max(),
                "retry test inserts a running row")) return false;
    original.recordId = recordId;
    const TerminalDecision decision{FocusRecordStatus::Success, 1500, 0, 3, 5};
    const FocusRecord terminal = withDecision(original, recordId, decision);

    database.failNextTerminalApplyForTesting();
    const bool firstFailed = !database.finalizeRecord(recordId, terminal);
    const bool retryApplied = database.finalizeRecord(recordId, terminal);
    const bool duplicateRetryApplied = database.finalizeRecord(recordId, terminal);
    const auto record = database.readById(recordId);
    bool ok = expect(firstFailed && retryApplied && duplicateRetryApplied,
                     "same terminal decision can be retried and replayed idempotently");
    ok &= expect(database.count() == 1, "idempotent retry does not duplicate history rows");
    ok &= expect(record.has_value() && matchesStoredRecord(*record, recordId, decision),
                 "idempotent retry preserves the selected terminal fields");
    database.close();

    DatabaseManager reopened(path.toStdString());
    ok &= expect(reopened.open(), "idempotence database opens after close");
    if (reopened.count() == 1) {
        const auto reopenedRecord = reopened.readById(recordId);
        ok &= expect(reopenedRecord.has_value() &&
                     matchesStoredRecord(*reopenedRecord, recordId, decision),
                     "idempotent result persists through another open");
    } else {
        ok &= expect(false, "idempotent reopen has one history row");
    }
    reopened.close();
    return ok;
}

bool testIntentWriteFailure(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("intent_write_failure"));
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "intent-write database opens")) return false;

    FocusRecord original = makeRunningRecord();
    const uint32_t recordId = database.append(original);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max(),
                "intent-write test inserts a running row")) return false;
    original.recordId = recordId;
    const TerminalDecision decision{FocusRecordStatus::Failed, 420, 2, 2, 0};
    const FocusRecord terminal = withDecision(original, recordId, decision);

    database.failNextTerminalIntentForTesting();
    const bool failedBeforeIntent = !database.finalizeRecord(recordId, terminal);
    const auto row = database.readById(recordId);
    qlonglong intentCount = -1;
    const bool inspectedIntent = queryInteger(
        path, QStringLiteral("SELECT COUNT(*) FROM focus_terminal_intents"), &intentCount);
    bool ok = expect(failedBeforeIntent, "injected intent-write failure is reported");
    ok &= expect(row.has_value() && row->status == FocusRecordStatus::Running,
                 "intent-write failure leaves the history row running");
    ok &= expect(inspectedIntent && intentCount == 0,
                 "failure before intent durability leaves no terminal decision");
    ok &= expect(database.finalizeRecord(recordId, terminal),
                 "the unchanged terminal decision can be retried after intent-write failure");
    const auto finalized = database.readById(recordId);
    ok &= expect(finalized.has_value() &&
                 matchesStoredRecord(*finalized, recordId, decision),
                 "retry after intent-write failure stores the chosen result");
    database.close();

    DatabaseManager reopened(path.toStdString());
    ok &= expect(reopened.open(), "intent-write recovery database reopens");
    const auto reopenedRecord = reopened.readById(recordId);
    ok &= expect(reopenedRecord.has_value() &&
                 matchesStoredRecord(*reopenedRecord, recordId, decision),
                 "successful retry survives reopen");
    reopened.close();
    return ok;
}

bool testChangedPendingIntentRejected(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("changed_pending_intent"));
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "pending-intent database opens")) return false;

    FocusRecord original = makeRunningRecord();
    const uint32_t recordId = database.append(original);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max(),
                "pending-intent test inserts a running row")) return false;
    original.recordId = recordId;
    const TerminalDecision chosen{FocusRecordStatus::Success, 1500, 0, 3, 5};
    const FocusRecord terminal = withDecision(original, recordId, chosen);

    database.failNextTerminalApplyForTesting();
    const bool firstFailed = !database.finalizeRecord(recordId, terminal);
    FocusRecord changed = terminal;
    ++changed.actualSeconds;
    const bool changedRejected = !database.finalizeRecord(recordId, changed);
    const auto row = database.readById(recordId);
    const bool stillRunning = row.has_value() && row->status == FocusRecordStatus::Running;
    database.close();

    if (!expect(firstFailed && changedRejected,
                "a different result cannot replace a durable pending decision")) return false;
    if (!expect(stillRunning, "rejected replacement leaves the original row recoverable")) return false;
    return recoverInChildAndVerify(path, recordId, chosen);
}

bool testCompletedRecordCannotBeChanged(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("completed_record_change"));
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "completed-record database opens")) return false;

    FocusRecord original = makeRunningRecord();
    const uint32_t recordId = database.append(original);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max(),
                "completed-record test inserts a running row")) return false;
    original.recordId = recordId;
    const TerminalDecision chosen{FocusRecordStatus::Success, 1500, 0, 3, 5};
    const FocusRecord terminal = withDecision(original, recordId, chosen);
    const bool finalized = database.finalizeRecord(recordId, terminal);

    FocusRecord illegalChange = terminal;
    illegalChange.status = FocusRecordStatus::Failed;
    illegalChange.actualSeconds = 900;
    illegalChange.coinsEarned = 0;
    const bool rejected = !database.finalizeRecord(recordId, illegalChange);
    const auto afterRejectedChange = database.readById(recordId);
    bool ok = expect(finalized, "initial terminal result is applied");
    ok &= expect(rejected, "completed terminal result cannot be changed");
    ok &= expect(afterRejectedChange.has_value() &&
                 matchesStoredRecord(*afterRejectedChange, recordId, chosen),
                 "rejected change leaves the completed result intact");
    database.close();

    DatabaseManager reopened(path.toStdString());
    ok &= expect(reopened.open(), "rejected completed-record change does not poison future open");
    const auto reopenedRecord = reopened.readById(recordId);
    ok &= expect(reopenedRecord.has_value() &&
                 matchesStoredRecord(*reopenedRecord, recordId, chosen),
                 "future open preserves the originally completed record");
    reopened.close();
    return ok;
}

bool testFutureSchemaRejected(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("future_schema"));
    const int futureVersion = DatabaseManager::schemaVersion() + 1;
    if (!expect(createVersionFixture(path, futureVersion, false),
                "future-version fixture is created")) return false;

    DatabaseManager database(path.toStdString());
    const bool opened = database.open();
    const bool errorReported = !database.lastError().isEmpty();
    database.close();
    qlonglong versionAfter = 0;
    const bool readVersion = queryInteger(
        path, QStringLiteral("SELECT version FROM schema_version"), &versionAfter);

    return expect(!opened && errorReported, "future focus schema is rejected") &&
           expect(readVersion && versionAfter == futureVersion,
                  "future schema version remains unchanged after rejection");
}

bool testV2MigratesToV3(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("schema_v2"));
    if (!expect(createVersionFixture(path, 2, true), "v2 fixture with a completed row is created")) {
        return false;
    }

    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "v2 database migrates and opens")) {
        std::cerr << database.lastError().toStdString() << '\n';
        return false;
    }
    const auto record = database.readById(7);
    bool ok = expect(database.count() == 1, "v2 migration preserves the existing record count");
    ok &= expect(record.has_value() && record->status == FocusRecordStatus::Success &&
                 record->actualSeconds == 777 && record->growthStage == 2 &&
                 record->coinsEarned == 2,
                 "v2 migration preserves existing terminal fields");
    database.close();

    qlonglong version = 0;
    qlonglong intentTableCount = 0;
    ok &= expect(queryInteger(path, QStringLiteral("SELECT version FROM schema_version"), &version) &&
                 version == DatabaseManager::schemaVersion(),
                 "v2 schema version advances to v3");
    ok &= expect(queryInteger(
                     path,
                     QStringLiteral("SELECT COUNT(*) FROM sqlite_master "
                                    "WHERE type='table' AND name='focus_terminal_intents'"),
                     &intentTableCount) &&
                 intentTableCount == 1,
                 "v3 migration creates the terminal-intent table");
    return ok;
}

bool testControllerFreezesAbandonedDecision(const QString& root)
{
    const QString path = databasePath(root, QStringLiteral("controller_abandon"));
    DatabaseManager database(path.toStdString());
    if (!expect(database.open(), "controller database opens")) return false;

    qint64 fakeNowMilliseconds = 0;
    FocusController::Clock clock{
        [&fakeNowMilliseconds]() { return fakeNowMilliseconds; },
        []() { return uint64_t{1700000000}; }
    };
    FocusController controller(database, clock);
    uint32_t recordId = std::numeric_limits<uint32_t>::max();
    uint32_t finalizedId = std::numeric_limits<uint32_t>::max();
    uint32_t finalizedStatus = std::numeric_limits<uint32_t>::max();
    int finalizedCount = 0;
    QObject::connect(&controller, &FocusController::sig_focusStarted,
                     [&recordId](uint32_t id) { recordId = id; });
    QObject::connect(&controller, &FocusController::sig_focusFinalized,
                     [&finalizedCount, &finalizedId, &finalizedStatus](uint32_t id, uint32_t status) {
                         ++finalizedCount;
                         finalizedId = id;
                         finalizedStatus = status;
                     });

    controller.startFocus(1, 10);
    if (!expect(recordId != std::numeric_limits<uint32_t>::max() &&
                controller.currentState() == FocusController::State::RUNNING,
                "controller starts a focus session")) return false;
    for (int i = 0; i < 17; ++i) { fakeNowMilliseconds += 1000; controller.tick(); }
    if (!expect(controller.actualSeconds() == 17, "controller accumulates the pre-decision ticks")) {
        return false;
    }

    database.failNextTerminalApplyForTesting();
    controller.abandonFocus();
    if (!expect(controller.currentState() == FocusController::State::SAVE_FAILED &&
                controller.hasPendingFinalization(),
                "failed abandoned save remains pending")) return false;

    TerminalDecision frozen;
    if (!expect(readIntent(path, recordId, &frozen),
                "failed controller save has a durable abandoned intent")) return false;
    bool ok = expect(frozen.status == FocusRecordStatus::Abandoned &&
                     frozen.actualSeconds == 17 && frozen.coinsEarned == 0,
                     "pending controller decision contains the abandoned result");

    for (int i = 0; i < 5; ++i) { fakeNowMilliseconds += 1000; controller.tick(); }
    controller.startFocus(2, 10);
    controller.completeFocus();
    controller.abandonFocus();
    controller.handleViolationDetected(QStringLiteral("ignored"));
    ok &= expect(controller.currentState() == FocusController::State::SAVE_FAILED &&
                 controller.hasPendingFinalization() && controller.actualSeconds() == 17,
                 "tick and alternate actions cannot change a pending decision");
    TerminalDecision afterBlockedActions;
    ok &= expect(readIntent(path, recordId, &afterBlockedActions) &&
                 sameDecision(frozen, afterBlockedActions),
                 "blocked actions leave the durable terminal intent unchanged");
    ok &= expect(database.count() == 1, "blocked start does not create another focus row");

    const bool retried = controller.retryPendingFinalization();
    ok &= expect(retried && !controller.hasPendingFinalization(),
                 "explicit retry finalizes the frozen decision");
    ok &= expect(finalizedCount == 1 && finalizedId == recordId &&
                 finalizedStatus == static_cast<uint32_t>(FocusRecordStatus::Abandoned),
                 "successful retry emits exactly one abandoned finalized signal");
    ok &= expect(controller.retryPendingFinalization() && finalizedCount == 1,
                 "retry after completion does not emit a duplicate finalized signal");

    const auto record = database.readById(recordId);
    ok &= expect(record.has_value() && matchesDecision(*record, frozen),
                 "controller retry applies the exact frozen terminal fields");
    ok &= expect(database.count() == 1, "controller retry keeps one history row");
    database.close();
    return ok;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() > 1 && (arguments[1] == QStringLiteral("--recover") ||
                                arguments[1] == QStringLiteral("--recover-and-settle"))) {
        return runRecoveryChild(arguments) ? 0 : 1;
    }

    QTemporaryDir directory;
    if (!expect(directory.isValid(), "temporary test directory is available")) return 1;

    const TerminalDecision decisions[] = {
        {FocusRecordStatus::Success, 1500, 0, 3, 5},
        {FocusRecordStatus::Failed, 420, 2, 2, 0},
        {FocusRecordStatus::Abandoned, 75, 1, 1, 0}
    };

    bool ok = true;
    ok &= testApplyFailureRestart(directory.path(), QStringLiteral("restart_success"), decisions[0]);
    ok &= testApplyFailureRestart(directory.path(), QStringLiteral("restart_failed"), decisions[1]);
    ok &= testApplyFailureRestart(directory.path(), QStringLiteral("restart_abandoned"), decisions[2]);
    ok &= testTerminalAndSettlementRestart(directory.path());
    ok &= testRetryIdempotence(directory.path());
    ok &= testIntentWriteFailure(directory.path());
    ok &= testChangedPendingIntentRejected(directory.path());
    ok &= testCompletedRecordCannotBeChanged(directory.path());
    ok &= testFutureSchemaRejected(directory.path());
    ok &= testV2MigratesToV3(directory.path());
    ok &= testControllerFreezesAbandonedDecision(directory.path());
    return ok ? 0 : 1;
}
