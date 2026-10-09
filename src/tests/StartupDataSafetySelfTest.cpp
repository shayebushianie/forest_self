#include "storage/StartupDataService.h"
#include "storage/ApplicationInstanceLock.h"
#include "common/DatabaseCommon.h"
#include "storage/DatabaseManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    if (!QDir().mkpath(QFileInfo(path).dir().absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool samePersistedFields(const FocusRecord& actual, const FocusRecord& expected)
{
    return actual.recordId == expected.recordId && actual.plantType == expected.plantType &&
           actual.plannedMinutes == expected.plannedMinutes &&
           actual.actualSeconds == expected.actualSeconds &&
           actual.startTimestamp == expected.startTimestamp && actual.status == expected.status &&
           actual.violationCount == expected.violationCount && actual.growthStage == expected.growthStage &&
           actual.coinsEarned == expected.coinsEarned && actual.tagId == expected.tagId &&
           actual.focusMode == expected.focusMode && actual.gridIndex == expected.gridIndex &&
           isInterruptedFocus(actual) == isInterruptedFocus(expected);
}

bool testLockBeforeCopy()
{
    QTemporaryDir temp;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    if (!temp.isValid() || !writeFile(legacy + "/users.dat", "legacy")) return false;
    QString error;
    StartupDataService owner(target);
    bool ok = expect(owner.acquire(&error), "target lock is acquired explicitly");
    ok &= expect(owner.acquire(&error), "target lock acquisition is idempotent");
    ok &= expect(owner.prepare({}, {}, nullptr, &error), "first instance prepares under target lock");
    StartupDataService blocked(target);
    ok &= expect(!blocked.prepare(legacy, {}, nullptr, &error), "second instance cannot prepare data");
    ok &= expect(!QFileInfo::exists(target + "/users.dat"), "blocked instance copies no account state");
    ok &= expect(readFile(legacy + "/users.dat") == "legacy", "blocked migration preserves source");
    return ok;
}

bool testCopyFailureAndRetry()
{
    QTemporaryDir temp;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    if (!temp.isValid() || !writeFile(legacy + "/user_1/sessions.dat", "records") ||
        !writeFile(target + "/user_1", "blocking-file")) return false;
    QString error;
    bool ok;
    {
        StartupDataService failed(target);
        ok = expect(!failed.prepare(legacy, {}, nullptr, &error), "copy failure aborts startup");
        ok &= expect(!error.isEmpty(), "copy failure is diagnosable");
        ok &= expect(readFile(legacy + "/user_1/sessions.dat") == "records", "copy failure keeps old records");
        ok &= expect(!QFileInfo::exists(target + "/legacy_migration.json"),
                     "copy failure does not mark migration complete");
    }
    // Remove only the artificial obstruction created by this fixture.
    if (!QFile::remove(target + "/user_1")) return false;
    StartupDataService retry(target);
    ok &= expect(retry.prepare(legacy, {}, nullptr, &error), "missing files retry after lock release");
    ok &= expect(readFile(target + "/user_1/sessions.dat") == "records", "retry copies complete records");
    ok &= expect(QFileInfo::exists(target + "/legacy_migration.json"),
                 "successful retry marks migration complete");
    return ok;
}

bool testAccountMigration()
{
    QTemporaryDir temp;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("old-users.dat");
    if (!temp.isValid() || !writeFile(legacy, "old")) return false;
    QString error;
    bool ok;
    {
        if (!QDir().mkpath(target + "/users.dat")) return false;
        StartupDataService failed(target);
        ok = expect(!failed.prepare({}, {legacy}, nullptr, &error), "account destination directory is an error");
    }
    if (!QDir().rmdir(target + "/users.dat")) return false;
    {
        StartupDataService migrated(target);
        ok &= expect(migrated.prepare({}, {legacy}, nullptr, &error), "account file migrates successfully");
    }
    if (!writeFile(target + "/users.dat", "current")) return false;
    StartupDataService existing(target);
    ok &= expect(existing.prepare({}, {legacy}, nullptr, &error), "existing account state wins");
    ok &= expect(readFile(target + "/users.dat") == "current", "old account does not overwrite current state");
    ok &= expect(readFile(legacy) == "old", "account source is retained");
    return ok;
}

bool testOneTimeMigrationAndOperationalExclusions()
{
    QTemporaryDir temp;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    if (!temp.isValid() || !writeFile(legacy + "/user_1/sessions.dat", "records") ||
        !writeFile(legacy + "/user_1/forest.lock", "nested lock") ||
        !writeFile(legacy + "/app.log", "log") || !writeFile(legacy + "/app.log.1", "rotated log") ||
        !writeFile(legacy + "/restore_request.txt", "restore") ||
        !writeFile(legacy + "/restore_transaction.json", "journal") ||
        !writeFile(legacy + "/legacy_migration.json", "not a source data file") ||
        !writeFile(legacy + "/ForestRestoreSnapshots/Restore-test/before.json", "snapshot journal") ||
        !writeFile(legacy + "/ForestRestoreSnapshots/Restore-test/before/users.dat", "snapshot data"))
        return false;

    QString error;
    bool ok;
    {
        StartupDataService first(target);
        ok = expect(first.prepare(legacy, {}, nullptr, &error), "initial migration succeeds");
    }
    ok &= expect(readFile(target + "/user_1/sessions.dat") == "records", "initial migration copies records");
    ok &= expect(!QFileInfo::exists(target + "/user_1/forest.lock"), "nested lock is not copied");
    ok &= expect(!QFileInfo::exists(target + "/app.log"), "logs are not copied");
    ok &= expect(!QFileInfo::exists(target + "/app.log.1"), "rotated logs are not copied");
    ok &= expect(!QFileInfo::exists(target + "/restore_request.txt"), "restore request is not copied");
    ok &= expect(!QFileInfo::exists(target + "/restore_transaction.json"), "restore journal is not copied");
    ok &= expect(!QFileInfo::exists(target + "/ForestRestoreSnapshots/Restore-test/before.json"),
                 "restore snapshot journal is not copied");
    ok &= expect(!QFileInfo::exists(target + "/ForestRestoreSnapshots/Restore-test/before/users.dat"),
                 "restore snapshot data is not copied");

    QFile markerFile(target + "/legacy_migration.json");
    if (!markerFile.open(QIODevice::ReadOnly)) return false;
    const QJsonObject marker = QJsonDocument::fromJson(markerFile.readAll()).object();
    ok &= expect(marker.value("version").toInt() == 1 && marker.value("complete").toBool(),
                 "completion marker is version 1 and complete");

    if (!QFile::remove(target + "/user_1/sessions.dat") ||
        !writeFile(legacy + "/user_1/sessions.dat", "updated records") ||
        !writeFile(legacy + "/user_2/coins.dat", "new source file")) return false;
    {
        StartupDataService later(target);
        ok &= expect(later.prepare(legacy, {}, nullptr, &error), "completed migration remains valid");
    }
    ok &= expect(!QFileInfo::exists(target + "/user_1/sessions.dat"),
                 "completed migration does not restore a missing target file");
    ok &= expect(!QFileInfo::exists(target + "/user_2/coins.dat"),
                 "completed migration ignores newly added source files");
    ok &= expect(readFile(legacy + "/user_1/sessions.dat") == "updated records",
                 "legacy source remains unchanged");
    return ok;
}

bool testCopiedSessionsImportExactFields()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return false;

    const QString current = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    const QString sourcePath = legacy + "/user_7/sessions.dat";
    FocusRecord source;
    source.recordId = 73;
    source.plantType = 6;
    source.plannedMinutes = 42;
    source.actualSeconds = 1873;
    source.startTimestamp = 1743554689;
    source.status = FocusRecordStatus::Success;
    source.violationCount = 2;
    source.growthStage = 5;
    source.coinsEarned = 17;
    source.tagId = 11;
    source.focusMode = PersistedFocusMode::GentleLegacy;
    source.gridIndex = 14;
    const QByteArray sourceBytes(reinterpret_cast<const char*>(&source), static_cast<int>(sizeof(source)));
    if (!writeFile(sourcePath, sourceBytes)) return false;

    QString error;
    StartupDataService startup(current);
    if (!expect(startup.prepare(legacy, {}, nullptr, &error),
                "old account directory is copied before session import")) return false;

    const QString copiedPath = current + "/user_7/sessions.dat";
    if (!expect(readFile(copiedPath) == sourceBytes, "copied sessions.dat retains the legacy record bytes"))
        return false;

    FocusRecord expected = source;
    expected.recordId = 0;
    DatabaseManager database(copiedPath.toStdString());
    if (!expect(database.open(), "DatabaseManager imports the copied sessions.dat")) {
        std::cerr << "DATABASE ERROR: " << database.lastError().toStdString() << '\n';
        return false;
    }
    const auto imported = database.readById(0);
    bool ok = expect(database.count() == 1, "copied legacy session imports exactly once");
    ok &= expect(imported.has_value() && samePersistedFields(*imported, expected),
                 "import preserves every persisted session field");
    ok &= expect(readFile(copiedPath) == sourceBytes, "SQLite import preserves copied sessions.dat");
    ok &= expect(readFile(sourcePath) == sourceBytes, "SQLite import preserves the old source record");
    return ok;
}

bool testCorruptAccountDoesNotChangeHealthyAccount()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return false;

    const QString damagedSessions = temp.filePath("accounts/user_1/sessions.dat");
    const QString healthySessions = temp.filePath("accounts/user_2/sessions.dat");
    FocusRecord healthySource;
    healthySource.recordId = 39;
    healthySource.plantType = 4;
    healthySource.plannedMinutes = 30;
    healthySource.actualSeconds = 1800;
    healthySource.startTimestamp = 1743554701;
    healthySource.status = FocusRecordStatus::Success;
    healthySource.violationCount = 1;
    healthySource.growthStage = 4;
    healthySource.coinsEarned = 9;
    healthySource.tagId = 5;
    healthySource.focusMode = PersistedFocusMode::Deep;
    healthySource.gridIndex = 8;
    FocusRecord healthyExpected = healthySource;
    healthyExpected.recordId = 0;

    QString healthyDatabasePath;
    {
        DatabaseManager healthy(healthySessions.toStdString());
        if (!expect(healthy.open(), "healthy account database is initialized")) return false;
        if (!expect(healthy.append(healthySource) == 0, "healthy account record is stored")) return false;
        healthyDatabasePath = healthy.databasePath();
    }
    const QByteArray healthyBytes = readFile(healthyDatabasePath);
    if (!expect(!healthyBytes.isEmpty(), "healthy account database fixture exists")) return false;

    DatabaseManager damaged(damagedSessions.toStdString());
    const QString damagedDatabasePath = damaged.databasePath();
    const QByteArray corruptedBytes("corrupt account database bytes");
    if (!writeFile(damagedDatabasePath, corruptedBytes)) return false;

    const bool opened = damaged.open();
    bool ok = expect(!opened, "corrupt account fails closed instead of loading partially");
    ok &= expect(!damaged.lastError().isEmpty(), "corrupt account reports a diagnostic");
    damaged.close();
    ok &= expect(readFile(damagedDatabasePath) == corruptedBytes,
                 "failed account load preserves the corrupted bytes");
    ok &= expect(readFile(healthyDatabasePath) == healthyBytes,
                 "failed account load leaves the other account database unchanged");

    DatabaseManager healthy(healthySessions.toStdString());
    if (!expect(healthy.open(), "other account remains independently loadable")) return false;
    const auto record = healthy.readById(0);
    ok &= expect(healthy.count() == 1 && record.has_value() &&
                     samePersistedFields(*record, healthyExpected),
                 "other account retains its exact healthy record");
    return ok;
}

bool testExplicitCompletionAndInvalidMarkers()
{
    QTemporaryDir temp;
    if (!temp.isValid()) return false;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    if (!writeFile(legacy + "/users.dat", "legacy")) return false;
    QString error;
    bool ok;
    {
        StartupDataService restored(target);
        ok = expect(restored.markMigrationComplete(&error), "restore path can mark migration complete");
        ok &= expect(restored.markMigrationComplete(&error), "marking migration complete is idempotent");
    }
    {
        StartupDataService afterRestore(target);
        ok &= expect(afterRestore.prepare(legacy, {}, nullptr, &error), "valid version 1 marker is accepted");
    }
    ok &= expect(!QFileInfo::exists(target + "/users.dat"), "restore completion suppresses legacy import");

    const QString futureTarget = temp.filePath("future");
    if (!writeFile(futureTarget + "/legacy_migration.json", "{\"version\":2,\"complete\":true}"))
        return false;
    StartupDataService future(futureTarget);
    error.clear();
    ok &= expect(!future.prepare(legacy, {}, nullptr, &error), "future marker stops startup migration");
    ok &= expect(!error.isEmpty(), "future marker reports a clear error");
    ok &= expect(!QFileInfo::exists(futureTarget + "/users.dat"), "future marker does not reimport source");

    const QString malformedTarget = temp.filePath("malformed");
    if (!writeFile(malformedTarget + "/legacy_migration.json", "{broken")) return false;
    StartupDataService malformed(malformedTarget);
    error.clear();
    ok &= expect(!malformed.prepare(legacy, {}, nullptr, &error), "malformed marker stops migration");
    ok &= expect(!error.isEmpty(), "malformed marker reports a clear error");
    ok &= expect(!QFileInfo::exists(malformedTarget + "/users.dat"),
                 "malformed marker does not reimport source");
    return ok;
}

bool testLegacySourceLock()
{
    QTemporaryDir temp;
    const QString target = temp.filePath("current");
    const QString legacy = temp.filePath("legacy");
    if (!temp.isValid() || !writeFile(legacy + "/users.dat", "legacy")) return false;

    QString error;
    ApplicationInstanceLock sourceOwner(legacy);
    bool ok = expect(sourceOwner.acquire(&error), "legacy source lock fixture is acquired");
    StartupDataService migrating(target);
    error.clear();
    ok &= expect(!migrating.prepare(legacy, {}, nullptr, &error), "active legacy source stops migration");
    ok &= expect(!error.isEmpty(), "active source lock reports a clear error");
    ok &= expect(readFile(legacy + "/users.dat") == "legacy", "active source data remains unchanged");
    ok &= expect(!QFileInfo::exists(target + "/users.dat"), "active source is not copied");

    const QString accountTarget = temp.filePath("account-target");
    StartupDataService accountMigration(accountTarget);
    ok &= expect(!accountMigration.prepare({}, {legacy + "/users.dat"}, nullptr, &error),
                 "standalone account source lock also stops migration");
    ok &= expect(!QFileInfo::exists(accountTarget + "/users.dat"),
                 "locked standalone account source is never copied");

    StartupDataService targetProbe(target);
    QString targetError;
    ok &= expect(!targetProbe.acquire(&targetError), "target lock remains held after source lock failure");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    bool ok = testLockBeforeCopy();
    ok &= testCopyFailureAndRetry();
    ok &= testAccountMigration();
    ok &= testOneTimeMigrationAndOperationalExclusions();
    ok &= testCopiedSessionsImportExactFields();
    ok &= testCorruptAccountDoesNotChangeHealthyAccount();
    ok &= testExplicitCompletionAndInvalidMarkers();
    ok &= testLegacySourceLock();
    return ok ? 0 : 1;
}
