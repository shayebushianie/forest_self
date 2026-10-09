#include "storage/DatabaseManager.h"
#include "storage/StorageEnvelope.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

constexpr quint32 kSessionMagic = 0x46534631u;
constexpr quint16 kUnsupportedSessionVersion = 99;

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << "\n";
    return condition;
}

bool expectOpen(DatabaseManager& database, const char* message)
{
    if (database.open()) return expect(true, message);
    std::cerr << "DATABASE ERROR: " << database.lastError().toStdString() << "\n";
    return expect(false, message);
}

class SqliteConnection {
public:
    explicit SqliteConnection(const QString& path)
        : connectionName_(QStringLiteral("migration_test_%1")
                              .arg(QUuid::createUuid().toString(QUuid::Id128))),
          database_(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_))
    {
        database_.setDatabaseName(path);
        open_ = database_.open();
    }

    ~SqliteConnection()
    {
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connectionName_);
    }

    bool isOpen() const { return open_; }

    bool exec(const QString& sql)
    {
        QSqlQuery query(database_);
        return query.exec(sql);
    }

    bool scalarInt(const QString& sql, int* value)
    {
        QSqlQuery query(database_);
        if (!query.exec(sql) || !query.next()) return false;
        *value = query.value(0).toInt();
        return true;
    }

    bool scalarString(const QString& sql, QString* value)
    {
        QSqlQuery query(database_);
        if (!query.exec(sql) || !query.next()) return false;
        *value = query.value(0).toString();
        return true;
    }

private:
    QString connectionName_;
    QSqlDatabase database_;
    bool open_ = false;
};

FocusRecord makeRecord(uint32_t actualSeconds, uint64_t timestamp)
{
    FocusRecord record;
    record.plantType = 3;
    record.plannedMinutes = 25;
    record.actualSeconds = actualSeconds;
    record.startTimestamp = timestamp;
    record.status = FocusRecordStatus::Success;
    record.growthStage = 5;
    record.coinsEarned = 8;
    return record;
}

QByteArray recordBytes(const FocusRecord& record)
{
    return QByteArray(reinterpret_cast<const char*>(&record), sizeof(record));
}

bool writeLegacyRecords(const QString& path, const std::vector<FocusRecord>& records)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    for (const FocusRecord& record : records) {
        const qint64 bytesWritten = file.write(reinterpret_cast<const char*>(&record), sizeof(record));
        if (bytesWritten != static_cast<qint64>(sizeof(record))) return false;
    }
    return file.flush();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
           file.write(bytes) == bytes.size() && file.flush();
}

bool createLegacySchema(const QString& path)
{
    SqliteConnection database(path);
    return database.isOpen() &&
           database.exec(QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)")) &&
           database.exec(QStringLiteral("INSERT INTO schema_version(version) VALUES(1)")) &&
           database.exec(QStringLiteral(
               "CREATE TABLE focus_records (record_id INTEGER PRIMARY KEY, plant_type INTEGER NOT NULL, "
               "planned_minutes INTEGER NOT NULL, actual_seconds INTEGER NOT NULL, "
               "start_timestamp INTEGER NOT NULL, status INTEGER NOT NULL, violation_count INTEGER NOT NULL, "
               "growth_stage INTEGER NOT NULL, coins_earned INTEGER NOT NULL, tag_id INTEGER NOT NULL, "
               "focus_mode INTEGER NOT NULL, grid_index INTEGER NOT NULL)"));
}

QString sqlitePathFor(const QString& legacyPath)
{
    const QFileInfo info(legacyPath);
    return info.dir().filePath(info.completeBaseName() + QStringLiteral(".sqlite"));
}

bool readScalarInt(const QString& path, const QString& sql, int* value)
{
    SqliteConnection database(path);
    return database.isOpen() && database.scalarInt(sql, value);
}

bool testBadLegacyRetriesAfterRestart(const QString& legacyPath)
{
    const FocusRecord record = makeRecord(731, 1710000000);
    QString error;
    if (!expect(StorageEnvelope::write(legacyPath, kSessionMagic, kUnsupportedSessionVersion,
                                       recordBytes(record), &error),
                "unsupported legacy fixture is written")) return false;

    {
        DatabaseManager firstAttempt(legacyPath.toStdString());
        if (!expect(!firstAttempt.open(), "unsupported legacy version is rejected")) return false;
    }

    if (!expect(writeLegacyRecords(legacyPath, {record}), "legacy fixture is repaired")) return false;
    DatabaseManager retry(legacyPath.toStdString());
    if (!expectOpen(retry, "legacy import retries after restart")) return false;
    if (!expect(retry.count() == 1, "restarted import writes the legacy record")) return false;
    const auto imported = retry.readById(0);
    if (!expect(imported && imported->actualSeconds == record.actualSeconds,
                "restarted import preserves the legacy record")) return false;

    QFile backup(legacyPath + QStringLiteral(".sqlite-migration.bak"));
    return expect(backup.open(QIODevice::ReadOnly), "legacy migration backup is retained") &&
           expect(backup.readAll() == recordBytes(record), "migration backup matches the source");
}

bool testInsertFailureRetriesAtomically(const QString& legacyPath)
{
    const std::vector<FocusRecord> records = {makeRecord(101, 1710000001), makeRecord(202, 1710000002)};
    if (!expect(writeLegacyRecords(legacyPath, records),
                "trigger fixture legacy records are written")) return false;

    const QString databasePath = sqlitePathFor(legacyPath);
    if (!expect(createLegacySchema(databasePath), "preexisting empty SQLite schema is created")) return false;
    {
        SqliteConnection database(databasePath);
        if (!expect(database.isOpen() &&
                        database.exec(QStringLiteral(
                            "CREATE TRIGGER reject_second_record BEFORE INSERT ON focus_records "
                            "WHEN NEW.record_id = 1 "
                            "BEGIN SELECT RAISE(ABORT, 'injected insert failure'); END")),
                    "insert failure trigger is installed")) return false;
    }

    {
        DatabaseManager firstAttempt(legacyPath.toStdString());
        if (!expect(!firstAttempt.open(), "trigger aborts the first legacy import")) return false;
        if (!expect(firstAttempt.lastError().contains(QStringLiteral("injected insert failure")),
                    "failed import reports the trigger error")) return false;
    }

    int rowCount = -1;
    if (!expect(
                    readScalarInt(databasePath,
                                  QStringLiteral("SELECT COUNT(*) FROM focus_records"), &rowCount) &&
                    rowCount == 0,
                "failed import rolls back all inserted records")) return false;
    if (!expect(readScalarInt(databasePath,
                              QStringLiteral("SELECT COUNT(*) FROM legacy_import_state"), &rowCount) &&
                    rowCount == 0,
                "failed import rolls back its completion marker")) return false;

    {
        SqliteConnection database(databasePath);
        if (!expect(database.isOpen() && database.exec(QStringLiteral("DROP TRIGGER reject_second_record")),
                    "failure trigger is removed for retry")) return false;
    }

    DatabaseManager retry(legacyPath.toStdString());
    if (!expectOpen(retry, "trigger-failed import retries after restart")) return false;
    if (!expect(retry.count() == 2, "retry imports each source record once")) return false;
    if (!expect(readScalarInt(
                    databasePath, QStringLiteral("SELECT COUNT(*) FROM legacy_import_state WHERE id = 1"),
                    &rowCount) &&
                    rowCount == 1,
                "successful retry persists one completion marker")) return false;

    QFile backup(legacyPath + QStringLiteral(".sqlite-migration.bak"));
    return expect(backup.open(QIODevice::ReadOnly), "backup survives failed import and retry") &&
           expect(backup.readAll() == QByteArray(
                      reinterpret_cast<const char*>(records.data()),
                      static_cast<int>(records.size() * sizeof(FocusRecord))),
                  "retry does not replace the original migration backup");
}

bool testExistingBackupIsPreserved(const QString& legacyPath)
{
    const FocusRecord sourceRecord = makeRecord(505, 1710000050);
    if (!expect(writeLegacyRecords(legacyPath, {sourceRecord}),
                "preexisting-backup source is written")) return false;

    const QString backupPath = legacyPath + QStringLiteral(".sqlite-migration.bak");
    const QByteArray originalBackup("preserved older backup");
    if (!expect(writeBytes(backupPath, originalBackup),
                "preexisting regular backup is written")) return false;

    DatabaseManager db(legacyPath.toStdString());
    if (!expectOpen(db, "migration accepts an existing regular backup")) return false;
    if (!expect(db.count() == 1, "source imports when an older backup already exists")) return false;

    QFile backup(backupPath);
    return expect(backup.open(QIODevice::ReadOnly), "preexisting backup remains available") &&
           expect(backup.readAll() == originalBackup,
                  "existing backup is preserved without comparing it to the current source");
}

bool testNonRegularBackupIsRejected(const QString& legacyPath)
{
    const FocusRecord sourceRecord = makeRecord(606, 1710000060);
    if (!expect(writeLegacyRecords(legacyPath, {sourceRecord}),
                "nonregular-backup source is written")) return false;

    const QString backupPath = legacyPath + QStringLiteral(".sqlite-migration.bak");
    if (!expect(QDir().mkpath(backupPath), "nonregular backup directory is created")) return false;

    {
        DatabaseManager db(legacyPath.toStdString());
        if (!expect(!db.open(), "migration rejects a nonregular backup target")) return false;
        if (!expect(db.lastError().contains(QStringLiteral("不是普通文件")),
                    "nonregular backup rejection is diagnosed")) return false;
    }

    const QString databasePath = sqlitePathFor(legacyPath);
    int count = -1;
    return expect(readScalarInt(databasePath, QStringLiteral("SELECT COUNT(*) FROM focus_records"), &count) &&
                      count == 0,
                  "nonregular backup rejection does not import records") &&
           expect(readScalarInt(
                      databasePath, QStringLiteral("SELECT COUNT(*) FROM legacy_import_state"), &count) &&
                      count == 0,
                  "nonregular backup rejection does not mark migration complete");
}

bool testExistingNonemptySqliteIsPreserved(const QString& legacyPath)
{
    const FocusRecord legacyRecord = makeRecord(111, 1710000011);
    if (!expect(writeLegacyRecords(legacyPath, {legacyRecord}), "compatibility legacy record is written")) {
        return false;
    }

    const QString databasePath = sqlitePathFor(legacyPath);
    if (!expect(createLegacySchema(databasePath),
                "unmarked version-one SQLite schema is created")) return false;
    {
        SqliteConnection database(databasePath);
        if (!expect(database.isOpen() &&
                        database.exec(QStringLiteral(
                            "INSERT INTO focus_records VALUES(0, 1, 25, 999, "
                            "1710000099, 0, 0, 5, 8, 0, 0, 0)")),
                    "existing SQLite record is inserted")) return false;
    }

    DatabaseManager db(legacyPath.toStdString());
    if (!expectOpen(db, "unmarked nonempty SQLite database opens compatibly")) return false;
    const auto preserved = db.readById(0);
    if (!expect(db.count() == 1 && preserved && preserved->actualSeconds == 999,
                "legacy import does not overwrite or mix with existing SQLite data")) return false;

    int version = 0;
    return expect(readScalarInt(
                      databasePath, QStringLiteral("SELECT version FROM schema_version"), &version) &&
                      version == DatabaseManager::schemaVersion(),
                  "known schema version upgrades to the current version");
}

bool testImportRunsOnlyOnce(const QString& legacyPath)
{
    const FocusRecord original = makeRecord(303, 1710000030);
    const FocusRecord appended = makeRecord(404, 1710000040);
    if (!expect(writeLegacyRecords(legacyPath, {original}),
                "one-time import source is written")) return false;

    {
        DatabaseManager firstOpen(legacyPath.toStdString());
        if (!expectOpen(firstOpen, "first open imports legacy history")) return false;
        if (!expect(firstOpen.count() == 1, "first open imports one source record")) return false;
        if (!expect(firstOpen.append(appended) == 1,
                    "new SQLite record is appended after migration")) return false;
    }

    DatabaseManager reopened(legacyPath.toStdString());
    if (!expectOpen(reopened, "database reopens after completed import")) return false;
    if (!expect(reopened.count() == 2, "completed import is not repeated on reopen")) return false;
    const auto imported = reopened.readById(0);
    const auto later = reopened.readById(1);
    return expect(imported && imported->actualSeconds == original.actualSeconds && later &&
                      later->actualSeconds == appended.actualSeconds,
                  "reopen preserves both imported and later SQLite records");
}

bool testFutureSchemaIsRejectedBeforeWrites(const QString& databasePath)
{
    {
        SqliteConnection database(databasePath);
        if (!expect(database.isOpen() &&
                        database.exec(QStringLiteral(
                            "CREATE TABLE SCHEMA_VERSION (version INTEGER NOT NULL)")) &&
                        database.exec(QStringLiteral("INSERT INTO schema_version(version) VALUES(99)")),
                    "future-version fixture is created")) return false;
    }

    {
        DatabaseManager db(databasePath.toStdString());
        if (!expect(!db.open(), "unknown future schema version is rejected")) return false;
        if (!expect(!db.lastError().isEmpty(), "future schema rejection reports an error")) return false;
    }

    int tableCount = 0;
    QString journalMode;
    SqliteConnection database(databasePath);
    return expect(database.isOpen() &&
                      database.scalarInt(QStringLiteral(
                          "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table'"), &tableCount) &&
                      tableCount == 1,
                  "future schema rejection creates no tables") &&
           expect(database.scalarString(QStringLiteral("PRAGMA journal_mode"), &journalMode) &&
                      journalMode.compare(QStringLiteral("delete"), Qt::CaseInsensitive) == 0,
                  "future schema rejection does not change the journal mode");
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        std::cerr << "FAILED: temporary test directory is unavailable\n";
        return 1;
    }

    const QDir directory(temporaryDirectory.path());
    bool ok = true;
    ok &= testBadLegacyRetriesAfterRestart(directory.filePath(QStringLiteral("bad_legacy_sessions.dat")));
    ok &= testInsertFailureRetriesAtomically(directory.filePath(QStringLiteral("trigger_sessions.dat")));
    ok &= testExistingBackupIsPreserved(directory.filePath(QStringLiteral("old_backup_sessions.dat")));
    ok &= testNonRegularBackupIsRejected(
        directory.filePath(QStringLiteral("nonregular_backup_sessions.dat")));
    ok &= testExistingNonemptySqliteIsPreserved(directory.filePath(QStringLiteral("existing_sessions.dat")));
    ok &= testImportRunsOnlyOnce(directory.filePath(QStringLiteral("once_sessions.dat")));
    ok &= testFutureSchemaIsRejectedBeforeWrites(directory.filePath(QStringLiteral("future.sqlite")));
    return ok ? 0 : 1;
}
