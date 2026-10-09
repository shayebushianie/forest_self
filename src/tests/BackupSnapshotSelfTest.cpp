#include "storage/BackupSnapshot.h"
#include "storage/DatabaseManager.h"
#include "storage/BackupService.h"
#include "storage/StorageEnvelope.h"
#include "storage/UserManager.h"
#include "core/CoinManager.h"
#include "core/FocusController.h"
#include "core/FocusResultService.h"
#include "core/FocusSettlementManager.h"
#include "core/ForestLayoutManager.h"
#include "core/GachaManager.h"
#include "core/GuardianManager.h"
#include "core/TagManager.h"
#include "core/UserFeatureServices.h"
#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include "utils/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
bool expect(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}
bool write(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
bool testCsvExport(const QString& root)
{
    DatabaseManager database(QDir(root).filePath("sessions.sqlite").toStdString());
    if (!database.open()) return false;

    const QByteArray header =
        "record_id,start_timestamp,plant_type,planned_minutes,actual_seconds,status,tag_id,coins_earned,interrupted\n";
    const auto readCsv = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        QByteArray bytes = file.readAll();
        bytes.replace("\r\n", "\n");
        return bytes;
    };
    QString error = QStringLiteral("stale error");
    const QString emptyOutput = QDir(root).filePath("empty.csv");
    bool ok = expect(BackupService::exportFocusCsv(database, emptyOutput, &error),
                     "empty database CSV export succeeds");
    ok &= expect(readCsv(emptyOutput) == header, "empty database CSV contains only the current header");
    ok &= expect(error.isEmpty(), "successful CSV export clears the supplied error");

    FocusRecord record;
    record.plantType = 2;
    record.plannedMinutes = 25;
    record.actualSeconds = 1499;
    record.startTimestamp = 1704067200;
    record.status = FocusRecordStatus::Abandoned;
    record.tagId = 17;
    std::memcpy(record.reserved, "INT1", 4);
    if (!expect(database.append(record) == 0, "CSV record fixture is stored")) return false;

    const QString recordOutput = QDir(root).filePath("record.csv");
    error = QStringLiteral("stale error");
    ok &= expect(BackupService::exportFocusCsv(database, recordOutput, &error),
                 "record CSV export succeeds");
    ok &= expect(readCsv(recordOutput) ==
                     header + "0,1704067200,2,25,1499,2,17,0,1\n",
                 "CSV export contains the flushed record fields, status, and interrupted flag");
    ok &= expect(error.isEmpty(), "successful record export clears the supplied error");

    database.close();
    const QString existingOutput = QDir(root).filePath("closed/existing.csv");
    const QString newOutput = QDir(root).filePath("closed/new.csv");
    if (!write(existingOutput, "preserve")) return false;
    error.clear();
    ok &= expect(!BackupService::exportFocusCsv(database, existingOutput, &error),
                 "closed database refuses CSV export");
    ok &= expect(!error.isEmpty(), "failed CSV query reports an error");
    ok &= expect(readCsv(existingOutput) == "preserve", "failed query preserves an existing export");
    error.clear();
    ok &= expect(!BackupService::exportFocusCsv(database, newOutput, &error) && !error.isEmpty(),
                 "closed database also refuses a new output path");
    ok &= expect(!QFileInfo::exists(newOutput), "failed query does not create a new export");
    return ok;
}
bool testWalAndInventory(const QString& root)
{
    const QString data = QDir(root).filePath("data");
    const QString user = QDir(data).filePath("user_1");
    const QString backup = QDir(root).filePath("backup");
    if (!QDir().mkpath(user)) return false;
    UserManager users(QDir(data).filePath("users.dat").toStdString());
    if (!users.open()) return false;
    DatabaseManager database(QDir(user).filePath("sessions.dat").toStdString());
    if (!database.open()) return false;
    FocusRecord success;
    success.status = FocusRecordStatus::Success;
    success.startTimestamp = 1704067200;
    success.actualSeconds = 1500;
    success.tagId = 9;
    if (database.append(success) != 0) return false;
    FocusRecord abandoned = success;
    abandoned.status = FocusRecordStatus::Abandoned;
    abandoned.actualSeconds = 120;
    if (database.append(abandoned) != 1) return false;
    CoinManager wallet;
    wallet.setDataPath(QDir(user).filePath("coins.dat").toStdString());
    if (!wallet.load() || !wallet.earn(25) ||
        !write(QDir(data).filePath("preferences.ini"), "[schema]\nversion=1\n") ||
        !write(QDir(data).filePath("notes.txt"), "unmanaged")) return false;
    bool ok = expect(QFileInfo(database.databasePath() + "-wal").size() > 0,
                     "fixture really has active WAL content");
    QString error;
    ok &= expect(BackupSnapshot::create(data, backup, &error), "live SQLite snapshot is created");
    if (!ok) { std::cerr << error.toStdString() << '\n'; return false; }
    BackupManifest manifest;
    ok &= expect(BackupSnapshot::validate(backup, &manifest, &error), "complete backup validates");
    ok &= expect(!QFileInfo::exists(QDir(backup).filePath("user_1/sessions.sqlite-wal")) &&
                 !QFileInfo::exists(QDir(backup).filePath("notes.txt")),
                 "backup is standalone and excludes operational/unknown files");
    const QString connection = QStringLiteral("snapshot_read_fixture");
    {
        auto copy = QSqlDatabase::addDatabase("QSQLITE", connection);
        copy.setDatabaseName(QDir(backup).filePath("user_1/sessions.sqlite"));
        copy.setConnectOptions("QSQLITE_OPEN_READONLY");
        ok &= expect(copy.open(), "snapshot opens without source WAL");
        QSqlQuery query(copy);
        ok &= expect(query.exec("SELECT tag_id,actual_seconds,status FROM focus_records ORDER BY record_id") &&
                     query.next() && query.value(0).toUInt() == 9 && query.value(1).toUInt() == 1500 &&
                     query.next() && query.value(1).toUInt() == 120 && query.value(2).toUInt() ==
                         static_cast<uint32_t>(FocusRecordStatus::Abandoned) && !query.next(),
                     "snapshot preserves committed records");
        query.finish();
        copy.close();
    }
    QSqlDatabase::removeDatabase(connection);
    // The original connection remains live; making a snapshot must not alter its records.
    ok &= expect(database.count() == 2, "snapshot leaves source records unchanged");
    ok &= expect(BackupSnapshot::validate(backup, &manifest, &error), "opening snapshot leaves a valid standalone backup");
    const QString preferences = QDir(backup).filePath("preferences.ini");
    ok &= expect(QFile::rename(preferences, preferences + ".missing"), "temporary missing-file fixture created");
    ok &= expect(!BackupSnapshot::validate(backup, &manifest, &error), "missing file is rejected");
    if (!QFile::rename(preferences + ".missing", preferences)) return false;
    if (!write(QDir(backup).filePath("extra.txt"), "extra")) return false;
    ok &= expect(!BackupSnapshot::validate(backup, &manifest, &error), "extra file is rejected");
    if (!QFile::remove(QDir(backup).filePath("extra.txt"))) return false;
    if (!write(preferences, "[schema]\nversion=1\nmodified=true\n")) return false;
    ok &= expect(!BackupSnapshot::validate(backup, &manifest, &error), "hash mismatch is rejected");
    return ok;
}

bool verifyRestartedFullAccount(const QString& data, const QString& backup, const QString& logPath,
                               uint32_t savedBalance)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::instance()->setProperty("forest.testDataRoot", data);
    Logger::instance().setLogPath(logPath.toStdString());
    Logger::instance().setSessionId("backup-restart-test");
    if (!Logger::instance().open()) return false;

    QString restored, error;
    if (!expect(BackupService::restoreIfRequested(data, &restored, &error),
                "restart applies the staged account restore")) {
        Logger::instance().close();
        return false;
    }
    bool ok = expect(restored == backup, "restart reports the requested snapshot");

    const QString userDir = QDir(data).filePath("user_1");
    UserManager users(QDir(data).filePath("users.dat").toStdString());
    if (!expect(users.open(), "restored account file reopens")) {
        Logger::instance().close();
        return false;
    }
    uint32_t userId = 0;
    if (!expect(users.loginUser("backup-restore-user", "fixture-password-only", userId) && userId == 1,
                "original account remains usable after restore")) {
        Logger::instance().close();
        return false;
    }
    uint32_t addedUserId = 0;
    ok &= expect(!users.loginUser("restore-added-user", "fixture-added-password", addedUserId),
                 "account added after backup is absent after restore");

    DatabaseManager database(QDir(userDir).filePath("sessions.dat").toStdString());
    if (!expect(database.open(), "restored focus database reopens")) {
        Logger::instance().close();
        return false;
    }
    std::vector<FocusRecord> records;
    const bool recordsRestored = database.getAllRecords(records) && records.size() == 1 &&
        records[0].status == FocusRecordStatus::Success && records[0].plantType == PlantTypeId::Pine &&
        records[0].plannedMinutes == 40 && records[0].actualSeconds == 2399 &&
        records[0].startTimestamp == 1704067200 && records[0].tagId == 1;
    ok &= expect(recordsRestored, "restart reopens the original focus record and no later record");

    CoinManager wallet;
    wallet.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
    const bool walletRestored = wallet.load() && wallet.balance() == savedBalance &&
        wallet.getTotalCoins() == savedBalance && wallet.hasFocusReward(0) &&
        wallet.isPlantUnlocked(PlantTypeId::Pine) && !wallet.isPlantUnlocked(PlantTypeId::Rose);
    ok &= expect(walletRestored, "restart reopens the saved wallet balance and plant unlocks");

    UserFeatureServices features(userDir, userId, users, database);
    if (!expect(features.load(), "restart reloads all account-scoped feature state")) return false;
    const auto* placement = features.forestLayout()->findPlacement(0);
    ok &= expect(features.tags()->name(1) == QStringLiteral("Restore project") &&
                     features.tags()->count() == 1 && features.gacha()->unlockedCount() == 1 &&
                     features.gacha()->isUnlocked(features.gacha()->allVariants().first().id) &&
                     placement && placement->posX == 20 && placement->posY == 30 &&
                     features.guardian()->dailyGoalMinutes() == 45 &&
                     features.guardian()->totalMinutes() == 39 &&
                     features.settlements()->isSettled(0),
                 "restart restores tags, variants, layout, guardian and settlement receipts");
    AchievementEngine achievements(database, wallet);
    FocusResultService results(database, wallet, achievements, features.gacha(), features.forestLayout(),
                               features.guardian(), features.challenges(), features.settlements());
    ok &= expect(results.recoverPendingRecords().isEmpty() && wallet.balance() == savedBalance,
                 "restored settlement is consistent and does not reissue rewards");

    UserPreferences& preferences = UserPreferences::instance();
    preferences.configure(data);
    preferences.bindAccount(userDir, userId, {userId});
    const bool preferencesLoaded = preferences.load();
    const auto focusSetup = preferences.focusSetup();
    const auto accessibility = preferences.accessibilityOptions();
    ok &= expect(preferencesLoaded && focusSetup.minutes == 40 && focusSetup.plantType == PlantTypeId::Pine &&
                     focusSetup.tagId == 1 && !preferences.allowPause(),
                 "restart reloads the backed-up account preferences");
    ok &= expect(preferencesLoaded && accessibility.fontScalePercent == 110 && accessibility.reducedMotion &&
                     !accessibility.highContrast,
                 "restart reloads the backed-up device preferences");

    Logger::instance().close();
    return ok;
}

bool testFullAccountBackupRestart(const QString& root)
{
    const QString data = QDir(root).filePath("data");
    const QString userDir = QDir(data).filePath("user_1");
    const QString backupRoot = QDir(root).filePath("backups");
    if (!QDir().mkpath(userDir) || !QDir().mkpath(backupRoot)) return false;
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::instance()->setProperty("forest.testDataRoot", data);

    UserManager users(QDir(data).filePath("users.dat").toStdString());
    if (!expect(users.open(), "temporary account store opens") ||
        !expect(users.registerUser("backup-restore-user", "fixture-password-only"),
                "temporary test account is registered")) return false;
    uint32_t userId = 0;
    if (!expect(users.loginUser("backup-restore-user", "fixture-password-only", userId) && userId == 1,
                "temporary account fixture logs in")) return false;

    DatabaseManager database(QDir(userDir).filePath("sessions.dat").toStdString());
    if (!expect(database.open(), "temporary focus database opens")) return false;
    UserFeatureServices features(userDir, userId, users, database);
    if (!expect(features.load() &&
                    features.tags()->addAndSave(QStringLiteral("Restore project")).tag.id == 1,
                "account feature fixture initializes with a real project tag")) return false;
    FocusRecord savedRecord;
    savedRecord.plantType = PlantTypeId::Pine;
    savedRecord.plannedMinutes = 40;
    savedRecord.actualSeconds = 2399;
    savedRecord.startTimestamp = 1704067200;
    savedRecord.status = FocusRecordStatus::Success;
    savedRecord.tagId = 1;
    savedRecord.coinsEarned = 4;
    if (!expect(database.append(savedRecord) == 0, "baseline focus record is stored")) return false;

    CoinManager wallet;
    wallet.setDataPath(QDir(userDir).filePath("coins.dat").toStdString());
    if (!expect(wallet.load(), "temporary wallet opens") ||
        !expect(wallet.earn(120), "baseline wallet is funded") ||
        !expect(wallet.unlockPlant(PlantTypeId::Pine), "baseline plant is unlocked")) return false;
    AchievementEngine achievements(database, wallet);
    FocusResultService results(database, wallet, achievements, features.gacha(), features.forestLayout(),
                               features.guardian(), features.challenges(), features.settlements());
    if (!expect(results.registerPendingRecord(0) && results.applyTerminalRecord(0).applied,
                "baseline record is settled through the production business service")) return false;
    features.forestLayout()->place(0, 20, 30);
    if (!expect(features.gacha()->unlockVariant(features.gacha()->allVariants().first().id) &&
                    features.guardian()->setDailyGoalMinutes(45),
                "baseline variant and guardian settings are prepared")) return false;
    const uint32_t savedBalance = wallet.balance();

    UserPreferences& preferences = UserPreferences::instance();
    preferences.configure(data);
    preferences.bindAccount(userDir, userId, {userId});
    if (!expect(preferences.load(), "temporary preferences open")) return false;
    UserPreferences::FocusSetup savedSetup;
    savedSetup.minutes = 40;
    savedSetup.plantType = PlantTypeId::Pine;
    savedSetup.tagId = 1;
    UserPreferences::AccessibilityOptions savedAccessibility;
    savedAccessibility.fontScalePercent = 110;
    savedAccessibility.reducedMotion = true;
    if (!expect(preferences.setFocusSetup(savedSetup) && preferences.setAllowPause(false),
                "baseline account preferences are saved") ||
        !expect(preferences.setAccessibilityOptions(savedAccessibility),
                "baseline device preferences are saved")) return false;

    QString backup, error;
    FocusController idleFocus(database);
    if (!expect(features.saveForBackup(idleFocus, wallet, &error),
                "production backup orchestration flushes all account state")) return false;
    if (!expect(BackupService::createBackup(data, backupRoot, &backup, &error),
                "full test-account backup is created")) {
        if (!error.isEmpty()) std::cerr << error.toStdString() << '\n';
        return false;
    }
    BackupManifest manifest;
    if (!expect(BackupSnapshot::validate(backup, &manifest, &error),
                "full account snapshot validates")) return false;
    bool completeInventory = true;
    for (const auto& path : {QStringLiteral("users.dat"), QStringLiteral("preferences.ini"),
                             QStringLiteral("user_1/preferences.ini"), QStringLiteral("user_1/coins.dat"),
                             QStringLiteral("user_1/sessions.sqlite"), QStringLiteral("user_1/tags.dat"),
                             QStringLiteral("user_1/variants.dat"), QStringLiteral("user_1/guardian.dat"),
                             QStringLiteral("user_1/focus_settlements.dat")}) {
        completeInventory &= manifest.files.contains(path);
    }
    if (!expect(completeInventory, "snapshot includes account, preferences, wallet and focus records")) {
        return false;
    }
    features.forestLayout()->movePlacement(0, 100, 100);
    if (!expect(features.tags()->rename(1, QStringLiteral("Changed project")) &&
                    features.gacha()->unlockVariant(features.gacha()->allVariants()[1].id) &&
                    features.guardian()->setDailyGoalMinutes(90) && features.save(),
                "post-backup account feature state is changed")) return false;

    FocusRecord laterRecord = savedRecord;
    laterRecord.startTimestamp += 3600;
    laterRecord.actualSeconds = 3000;
    if (!expect(database.append(laterRecord) == 1, "post-backup focus record is stored") ||
        !expect(wallet.earn(37) && wallet.unlockPlant(PlantTypeId::Rose),
                "post-backup wallet and plant state are changed")) return false;
    UserPreferences::FocusSetup changedSetup = savedSetup;
    changedSetup.minutes = 55;
    changedSetup.plantType = PlantTypeId::Rose;
    changedSetup.tagId = 29;
    UserPreferences::AccessibilityOptions changedAccessibility = savedAccessibility;
    changedAccessibility.fontScalePercent = 125;
    changedAccessibility.highContrast = true;
    if (!expect(preferences.setFocusSetup(changedSetup) && preferences.setAllowPause(true),
                "post-backup account preferences are changed") ||
        !expect(preferences.setAccessibilityOptions(changedAccessibility),
                "post-backup device preferences are changed") ||
        !expect(users.registerUser("restore-added-user", "fixture-added-password"),
                "post-backup account file is changed")) return false;

    database.close();
    users.close();
    if (!expect(BackupService::requestRestore(data, backup, &error), "restore is staged for restart")) {
        if (!error.isEmpty()) std::cerr << error.toStdString() << '\n';
        return false;
    }

    QProcess restart;
    const QString restartLog = QDir(root).filePath("restart-events.log");
    restart.start(QCoreApplication::applicationFilePath(),
                  {QStringLiteral("--restore-full-account"), data, backup, restartLog,
                   QString::number(savedBalance)});
    if (!restart.waitForStarted(5000)) {
        std::cerr << restart.errorString().toStdString() << '\n';
        return false;
    }
    if (!restart.waitForFinished(15000)) {
        restart.kill();
        restart.waitForFinished();
    }
    const bool restarted = restart.exitStatus() == QProcess::NormalExit && restart.exitCode() == 0;
    if (!restarted) {
        const QByteArray output = restart.readAllStandardOutput() + restart.readAllStandardError();
        if (!output.isEmpty()) std::cerr << output.constData();
    }
    return expect(restarted, "restart restores and reopens the complete test account");
}

bool testFormats(const QString& root)
{
    QString error;
    const QString futureSettings = QDir(root).filePath("future-settings");
    if (!write(QDir(futureSettings).filePath("preferences.ini"), "[schema]\nversion=99\n")) return false;
    bool ok = expect(!BackupSnapshot::create(futureSettings, futureSettings + "-copy", &error),
                     "future preference version is rejected");
    const QString futureWallet = QDir(root).filePath("future-wallet/user_1/coins.dat");
    if (!StorageEnvelope::write(futureWallet, 0x46574C31u, 99, "future", &error)) return false;
    ok &= expect(!BackupSnapshot::create(QDir(root).filePath("future-wallet"),
                                         QDir(root).filePath("future-wallet-copy"), &error),
                 "future state envelope version is rejected");
    const QString malformed = QDir(root).filePath("malformed/user_1/coins.dat");
    if (!StorageEnvelope::write(malformed, 0x46574C31u, 2, "payload", &error) ||
        !BackupSnapshot::copyFile(malformed, malformed + ".bak", &error)) return false;
    if (!write(malformed, "damaged")) return false;
    const auto inspected = StorageEnvelope::inspect(malformed, 0x46574C31u);
    ok &= expect(inspected.state != StorageEnvelope::ReadState::RecoveredFromBackup,
                 "read-only validation never repairs from .bak");
    QFile damaged(malformed);
    ok &= expect(damaged.open(QIODevice::ReadOnly) && damaged.readAll() == "damaged",
                 "inspect preserves damaged source bytes");
    ok &= expect(!BackupSnapshot::create(QDir(root).filePath("malformed"),
                                         QDir(root).filePath("malformed-copy"), &error),
                 "unverifiable source is not published as a valid backup");
    const QString futureSqlite = QDir(root).filePath("future-sqlite/user_1/sessions.sqlite");
    {
        DatabaseManager db(futureSqlite.toStdString());
        if (!db.open()) return false;
        const auto connection = QStringLiteral("snapshot_future_fixture");
        {
            auto sql = QSqlDatabase::addDatabase("QSQLITE", connection);
            sql.setDatabaseName(futureSqlite);
            if (!sql.open()) return false;
            QSqlQuery query(sql);
            if (!query.exec("UPDATE schema_version SET version=99")) return false;
            sql.close();
        }
        QSqlDatabase::removeDatabase(connection);
        ok &= expect(!BackupSnapshot::create(QDir(root).filePath("future-sqlite"),
                                             QDir(root).filePath("future-sqlite-copy"), &error),
                     "future SQLite schema is rejected without migration");
    }
    return ok;
}
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 6 && app.arguments()[1] == "--restore-full-account") {
        bool balanceOk = false;
        const uint32_t savedBalance = app.arguments()[5].toUInt(&balanceOk);
        return balanceOk && verifyRestartedFullAccount(app.arguments()[2], app.arguments()[3],
                                                       app.arguments()[4], savedBalance) ? 0 : 1;
    }
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    const QString logPath = QDir(sandbox.path()).filePath("backup-events.log");
    Logger::instance().setLogPath(logPath.toStdString());
    Logger::instance().setSessionId("backup-test");
    if (!Logger::instance().open()) return 1;
    bool ok = testCsvExport(QDir(sandbox.path()).filePath("csv"));
    ok &= testWalAndInventory(QDir(sandbox.path()).filePath("wal"));
    ok &= testFullAccountBackupRestart(QDir(sandbox.path()).filePath("full-account"));
    ok &= testFormats(QDir(sandbox.path()).filePath("formats"));
    Logger::instance().close();
    QFile log(logPath);
    if (!log.open(QIODevice::ReadOnly)) return 1;
    const QByteArray events = log.readAll();
    ok &= expect(events.contains("[module=backup] [operation=export-complete]") &&
                 events.contains("[module=backup] [operation=export-read]"), "export outcome events are logged");
    ok &= expect(!events.contains(sandbox.path().toUtf8()), "backup logs omit local paths");
    return ok ? 0 : 1;
}
