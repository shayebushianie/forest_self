#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/FocusResultService.h"
#include "core/FocusSettlementManager.h"
#include "core/GuardianManager.h"
#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QTemporaryDir>

#include <iostream>
#include <vector>

namespace {
constexpr quint32 kGuardianMagic = 0x46474431u;

bool expect(bool ok, const char* message)
{
    if (!ok) std::cerr << "FAILED: " << message << '\n';
    return ok;
}

FocusRecord makeRecord(QDate date, FocusRecordStatus status, uint32_t seconds)
{
    FocusRecord record;
    record.status = status;
    record.actualSeconds = seconds;
    record.startTimestamp = static_cast<uint64_t>(
        QDateTime(date, QTime(12, 0), Qt::LocalTime).toSecsSinceEpoch());
    return record;
}

bool testOutOfOrderSecondsAndIdempotence()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString root = temporary.path();
    DatabaseManager database(QDir(root).filePath("sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(root).filePath("coins.dat").toStdString());
    GuardianManager guardian(QDir(root).filePath("guardian.dat"));
    if (!database.open() || !wallet.load() || !guardian.load()) return false;
    if (!guardian.setDailyGoalMinutes(30)) return false;

    const QDate today = QDate::currentDate();
    auto append = [&](QDate date, FocusRecordStatus status, uint32_t seconds) {
        return database.append(makeRecord(date, status, seconds));
    };
    const uint32_t newest = append(today.addDays(-1), FocusRecordStatus::Success, 1800);
    const uint32_t middle = append(today.addDays(-2), FocusRecordStatus::Success, 1800);
    const uint32_t shortA = append(today.addDays(-2), FocusRecordStatus::Success, 31);
    const uint32_t oldest = append(today.addDays(-3), FocusRecordStatus::Success, 1800);
    const uint32_t shortB = append(today.addDays(-2), FocusRecordStatus::Success, 31);
    const uint32_t failed = append(today, FocusRecordStatus::Failed, 3600);
    const uint32_t abandoned = append(today, FocusRecordStatus::Abandoned, 3600);
    const uint32_t running = append(today, FocusRecordStatus::Running, 3600);
    guardian.checkDayBoundary(today);

    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr);
    bool ok = true;
    for (uint32_t id : {newest, shortA, oldest, middle, shortB}) {
        const auto outcome = service.applyTerminalRecord(id);
        if (!outcome.applied) std::cerr << "record " << id << ": " << outcome.error.toStdString() << '\n';
        ok &= expect(outcome.applied, "successful focus settles in arbitrary record order");
    }
    ok &= expect(guardian.totalMinutes() == 91,
                 "effective seconds aggregate across records before whole-minute conversion");
    ok &= expect(guardian.todayMinutes() == 0,
                 "late settlement uses the record local start date, not compensation date");
    ok &= expect(guardian.currentStreak() == 3 && guardian.longestStreak() == 3,
                 "three consecutive successful goal-days produce a streak of three");
    ok &= expect(guardian.hasCompletedFocus(shortA) && guardian.hasCompletedFocus(shortB),
                 "sub-minute successful records still persist settlement receipts");

    const uint32_t beforeRetry = guardian.totalMinutes();
    const auto repeated = service.applyTerminalRecord(newest);
    ok &= expect(repeated.applied && repeated.alreadyApplied && guardian.totalMinutes() == beforeRetry,
                 "duplicate terminal settlement does not duplicate guardian seconds");
    const auto failedOutcome = service.applyTerminalRecord(failed);
    const auto abandonedOutcome = service.applyTerminalRecord(abandoned);
    const auto runningOutcome = service.applyTerminalRecord(running);
    ok &= expect(failedOutcome.applied && abandonedOutcome.applied && !runningOutcome.applied &&
                 guardian.totalMinutes() == beforeRetry && !guardian.hasCompletedFocus(failed),
                 "failed, abandoned, and running records contribute no guardian seconds");

    GuardianManager reloaded(QDir(root).filePath("guardian.dat"));
    ok &= expect(reloaded.load() && reloaded.hasCompletedFocus(shortA) &&
                 reloaded.totalMinutes() == beforeRetry,
                 "v3 receipts and exact lifetime totals survive reload");
    return ok;
}

bool testGapsAndGoalSnapshots()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    GuardianManager guardian(QDir(temporary.path()).filePath("guardian.dat"));
    if (!guardian.load() || !guardian.setDailyGoalMinutes(30)) return false;
    const QDate today = QDate::currentDate();
    std::vector<FocusRecord> records = {
        makeRecord(today.addDays(-5), FocusRecordStatus::Success, 1800),
        makeRecord(today.addDays(-4), FocusRecordStatus::Success, 1800),
        makeRecord(today.addDays(-3), FocusRecordStatus::Success, 1800),
        makeRecord(today.addDays(-1), FocusRecordStatus::Success, 1800)
    };
    records[0].recordId = 10;
    records[1].recordId = 11;
    records[2].recordId = 12;
    records[3].recordId = 13;
    bool ok = true;
    for (const auto& record : records) {
        ok &= expect(guardian.addCompletedFocusSeconds(record.recordId, record.actualSeconds,
            QDateTime::fromSecsSinceEpoch(record.startTimestamp).date()), "historical receipt settles");
    }
    ok &= expect(guardian.currentStreak() == 1 && guardian.longestStreak() == 3,
                 "a missed date breaks current streak without losing the prior longest streak");
    ok &= expect(guardian.todayMinutes() == 0, "empty current day remains at zero");

    ok &= expect(guardian.setDailyGoalMinutes(60), "goal change persists");
    ok &= expect(guardian.reconcileCompletedFocusRecords(records), "goal change reconciliation persists");
    ok &= expect(guardian.longestStreak() == 3,
                 "raising the goal never retroactively revokes the earned longest streak");

    ok &= expect(guardian.addCompletedFocusSeconds(14, 1800, today), "new day records exact seconds");
    ok &= expect(guardian.todayMinutes() == 30 && guardian.currentStreak() == 1,
                 "new date snapshots the new goal while yesterday retains its original goal");
    ok &= expect(guardian.addCompletedFocusSeconds(15, 1800, today), "same-day seconds accumulate");
    ok &= expect(guardian.todayMinutes() == 60 && guardian.currentStreak() == 2 &&
                 guardian.longestStreak() == 3,
                 "same-day goal crossing extends current streak without rewriting older goals");

    const uint32_t beforeLate = guardian.totalMinutes();
    ok &= expect(guardian.addCompletedFocusSeconds(16, 600, today.addDays(-4)),
                 "out-of-order historical receipt is accepted");
    ok &= expect(guardian.totalMinutes() == beforeLate + 10 && guardian.todayMinutes() == 60,
                 "late historical receipt changes lifetime/date history, not today's minutes");
    return ok;
}

bool testV2MigrationAndSaveRollback()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString path = QDir(temporary.path()).filePath("guardian.dat");
    const QDate today = QDate::currentDate();
    QByteArray v2Payload;
    QBuffer buffer(&v2Payload);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QDataStream stream(&buffer);
    stream << quint32(30) << quint32(30) << quint32(1) << quint32(4) << quint32(30)
           << static_cast<qint64>(today.toJulianDay()) << quint32(1) << quint32(77);
    if (stream.status() != QDataStream::Ok ||
        !StorageEnvelope::write(path, kGuardianMagic, 2, v2Payload)) return false;

    GuardianManager guardian(path);
    bool ok = expect(guardian.load(), "v2 guardian state migrates");
    const auto migrated = StorageEnvelope::inspect(path, kGuardianMagic);
    ok &= expect(migrated.hasPayload() && migrated.version == 3 && guardian.hasCompletedFocus(77) &&
                 guardian.longestStreak() == 4 && guardian.totalMinutes() == 30,
                 "v2 receipt and earned summaries survive v3 migration");

    StorageEnvelope::failNextWriteForTesting(path);
    ok &= expect(!guardian.setDailyGoalMinutes(60) && guardian.dailyGoalMinutes() == 30,
                 "failed goal save rolls back the in-memory goal");

    StorageEnvelope::failNextWriteForTesting(path);
    ok &= expect(!guardian.addCompletedFocusSeconds(78, 31, today) &&
                 !guardian.hasCompletedFocus(78) && guardian.totalMinutes() == 30 &&
                 guardian.todayMinutes() == 30,
                 "failed first-day receipt save rolls back seconds and receipt");

    StorageEnvelope::failNextWriteForTesting(path);
    ok &= expect(!guardian.checkDayBoundary(today.addDays(1)) && guardian.currentStreak() == 1 &&
                 guardian.todayMinutes() == 30,
                 "failed day-boundary save rolls back its in-memory date summary");

    QBuffer verifyBuffer;
    const auto afterFailure = StorageEnvelope::inspect(path, kGuardianMagic);
    ok &= expect(afterFailure.hasPayload() && afterFailure.payload == migrated.payload,
                 "all failed writes leave the persisted payload unchanged");
    verifyBuffer.setData(afterFailure.payload);
    verifyBuffer.open(QIODevice::ReadOnly);
    QDataStream verify(&verifyBuffer);
    quint32 goal = 0, todayMinutes = 0, current = 0, longest = 0, total = 0, receiptCount = 0;
    qint64 savedDay = 0;
    verify >> goal >> todayMinutes >> current >> longest >> total >> savedDay >> receiptCount;
    ok &= expect(verify.status() == QDataStream::Ok && savedDay == today.toJulianDay(),
                 "failed day-boundary write leaves the persisted date unchanged");
    return ok;
}

bool testSubMinuteConfirmedCompatibility()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString root = temporary.path();
    DatabaseManager database(QDir(root).filePath("sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(root).filePath("coins.dat").toStdString());
    GuardianManager guardian(QDir(root).filePath("guardian.dat"));
    if (!database.open() || !wallet.load() || !guardian.load()) return false;
    FocusRecord shortFocus = makeRecord(QDate::currentDate().addDays(-1), FocusRecordStatus::Success, 31);
    FocusSettlementManager settlements(QDir(root).filePath("settlements.dat"), database);
    if (!settlements.load()) return false;
    const uint32_t recordId = database.append(shortFocus);
    QByteArray legacyPayload;
    QDataStream legacy(&legacyPayload, QIODevice::WriteOnly);
    legacy << quint32(30) << quint32(0) << quint32(0) << quint32(0) << quint32(0)
           << static_cast<qint64>(QDate::currentDate().toJulianDay()) << quint32(0);
    if (!StorageEnvelope::write(QDir(root).filePath("guardian.dat"), kGuardianMagic, 2,
                                legacyPayload) || !guardian.load()) return false;
    if (!settlements.load() || !settlements.markPending(recordId) || !settlements.markSettled(recordId)) return false;

    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr,
                               &guardian, nullptr, &settlements);
    const auto outcomes = service.recoverPendingRecords();
    return expect(outcomes.isEmpty() && guardian.hasCompletedFocus(recordId),
                  "legacy confirmed sub-minute record is accepted then receives its v3 receipt") &&
           expect(guardian.todayMinutes() == 0 && guardian.totalMinutes() == 0,
                  "31 effective seconds are receipted without rounding up to a minute");
}

bool testNewReceiptLossIsNotReconciledAway()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString root = temporary.path();
    DatabaseManager database(QDir(root).filePath("sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(root).filePath("coins.dat").toStdString());
    const QString guardianPath = QDir(root).filePath("guardian.dat");
    GuardianManager guardian(guardianPath);
    if (!database.open() || !wallet.load() || !guardian.load() || !guardian.save()) return false;
    FocusSettlementManager settlements(QDir(root).filePath("settlements.dat"), database);
    if (!settlements.load()) return false;
    const uint32_t id = database.append(makeRecord(QDate::currentDate(), FocusRecordStatus::Success, 31));
    if (!settlements.load() || !settlements.markPending(id) || !settlements.markSettled(id)) return false;
    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &settlements);
    const auto before = StorageEnvelope::inspect(guardianPath, kGuardianMagic).payload;
    const auto outcomes = service.recoverPendingRecords();
    return expect(outcomes.size() == 1 && !outcomes[0].applied && !outcomes[0].error.isEmpty() &&
                  !guardian.hasCompletedFocus(id), "new missing sub-minute receipt stops compensation") &&
           expect(StorageEnvelope::inspect(guardianPath, kGuardianMagic).payload == before,
                  "reconciliation never masks missing new settlement evidence");
}

bool testLegacyWrongDateIsCorrectedOnce()
{
    QTemporaryDir temporary;
    if (!temporary.isValid()) return false;
    const QString root = temporary.path();
    DatabaseManager database(QDir(root).filePath("sessions.dat").toStdString());
    CoinManager wallet;
    wallet.setDataPath(QDir(root).filePath("coins.dat").toStdString());
    const QString guardianPath = QDir(root).filePath("guardian.dat");
    const QDate today = QDate::currentDate();
    if (!database.open() || !wallet.load()) return false;
    FocusSettlementManager settlements(QDir(root).filePath("settlements.dat"), database);
    if (!settlements.load()) return false;
    const uint32_t id = database.append(makeRecord(today.addDays(-1), FocusRecordStatus::Success, 1800));
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << quint32(30) << quint32(30) << quint32(1) << quint32(4) << quint32(30)
           << static_cast<qint64>(today.toJulianDay()) << quint32(1) << quint32(id);
    if (!StorageEnvelope::write(guardianPath, kGuardianMagic, 2, payload)) return false;
    GuardianManager guardian(guardianPath);
    if (!guardian.load() || !settlements.load() || !settlements.markPending(id) ||
        !settlements.markSettled(id)) return false;
    AchievementEngine achievements(database, wallet);
    FocusResultService service(database, wallet, achievements, nullptr, nullptr, &guardian, nullptr, &settlements);
    StorageEnvelope::failNextWriteForTesting(guardianPath);
    const auto failed = service.recoverPendingRecords();
    bool ok = expect(failed.size() == 1 && !failed[0].applied && guardian.needsHistoryReconciliation(),
                     "failed migration reconciliation remains pending for restart");
    ok &= expect(service.recoverPendingRecords().isEmpty() && guardian.todayMinutes() == 0 &&
                 guardian.totalMinutes() == 30 && guardian.currentStreak() == 1 &&
                 guardian.longestStreak() == 4, "old compensation date is replaced with actual start date");
    GuardianManager reloaded(guardianPath);
    ok &= expect(reloaded.load() && !reloaded.needsHistoryReconciliation() &&
                 reloaded.todayMinutes() == 0 && reloaded.hasCompletedFocus(id),
                 "migration completion and receipt survive reload");
    return ok;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = testOutOfOrderSecondsAndIdempotence();
    ok &= testGapsAndGoalSnapshots();
    ok &= testV2MigrationAndSaveRollback();
    ok &= testSubMinuteConfirmedCompatibility();
    ok &= testNewReceiptLossIsNotReconciledAway();
    ok &= testLegacyWrongDateIsCorrectedOnce();
    return ok ? 0 : 1;
}
