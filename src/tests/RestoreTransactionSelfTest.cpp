#include "storage/BackupService.h"
#include "storage/BackupSnapshot.h"
#include "storage/RestoreTransaction.h"
#include "storage/StartupDataService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}
bool write(const QString& root, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(root).filePath(name);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& root, const QString& name)
{
    QFile file(QDir(root).filePath(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool restartRecovery(const QString& data)
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {"--recover", data});
    return child.waitForFinished(15000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
}
bool runScenario(const QString& root, RestoreTransaction::Fault::Point point, int cut)
{
    const QString source = QDir(root).filePath("source");
    const QString data = QDir(root).filePath("data");
    const QString backup = QDir(root).filePath("backup");
    if (!write(source, "ui_settings.ini", "[test]\nvalue=restored\n") ||
        !write(source, "preferences.ini", "[test]\nvalue=restored\n") ||
        !write(data, "ui_settings.ini", "[test]\nvalue=original\n") ||
        !write(data, "preferences.ini", "[test]\nvalue=original\n") ||
        !write(data, "user_99/coins.dat", "damaged-but-retained-original") ||
        !write(data, "user_99/sessions.sqlite-wal", "original-wal") ||
        !write(data, "user_99/sessions.sqlite-shm", "original-shm") ||
        !write(data, "notes.txt", "never-managed") ||
        !write(data, "restore_request.txt", backup.toUtf8())) return false;
    QString error, restored;
    if (!BackupSnapshot::create(source, backup, &error)) {
        std::cerr << error.toStdString() << '\n';
        return false;
    }
    RestoreTransaction::Fault fault{point, cut};
    const bool applied = RestoreTransaction::apply(data, backup, &restored, &error, &fault);
    bool ok = expect(!applied, "injected failure is reported");
    ok &= expect(restartRecovery(data), "restart completes persistent rollback/commit cleanup");
    const bool committed = point == RestoreTransaction::Fault::Point::AfterCommit;
    const QByteArray expected = committed ? "[test]\nvalue=restored\n" : "[test]\nvalue=original\n";
    ok &= expect(read(data, "ui_settings.ini") == expected && read(data, "preferences.ini") == expected,
                 "restart never leaves mixed original/restored states");
    ok &= expect(read(data, "notes.txt") == "never-managed", "unknown target files remain untouched");
    ok &= expect(committed ? !QFileInfo::exists(QDir(data).filePath("user_99/coins.dat")) :
                 read(data, "user_99/coins.dat") == "damaged-but-retained-original",
                 "extra managed data is isolated on commit and restored on rollback");
    ok &= expect(committed ? !QFileInfo::exists(QDir(data).filePath("user_99/sessions.sqlite-wal")) :
                 read(data, "user_99/sessions.sqlite-wal") == "original-wal",
                 "stale WAL cannot attach to a restored database, rollback preserves it");
    ok &= expect(committed ? !QFileInfo::exists(QDir(data).filePath("user_99/sessions.sqlite-shm")) :
                 read(data, "user_99/sessions.sqlite-shm") == "original-shm",
                 "SQLite shared memory is isolated with its original database");
    ok &= expect(!QFileInfo::exists(QDir(data).filePath("restore_transaction.json")) &&
                 !QFileInfo::exists(QDir(data).filePath("restore_request.txt")),
                 "finished transaction cannot replay automatically");
    const auto snapshots = QDir(QDir(root).filePath("ForestRestoreSnapshots"))
        .entryList({"Restore-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    ok &= expect(snapshots.size() == 1, "recovery preimage remains available");
    return ok;
}

bool testCorruptRecoveryMetadata(const QString& root)
{
    const QString data = QDir(root).filePath("data");
    const QString source = QDir(root).filePath("source");
    const QString backup = QDir(root).filePath("backup");
    if (!write(source, "ui_settings.ini", "[test]\nvalue=new\n") ||
        !write(data, "ui_settings.ini", "[test]\nvalue=old\n")) return false;
    QString restored, error;
    if (!BackupSnapshot::create(source, backup, &error)) return false;
    RestoreTransaction::Fault fault{RestoreTransaction::Fault::Point::ApplyInterruption, 1};
    if (RestoreTransaction::apply(data, backup, &restored, &error, &fault)) return false;
    const QString snapshots = QDir(root).filePath("ForestRestoreSnapshots");
    const auto entries = QDir(snapshots).entryList({"Restore-*"}, QDir::Dirs | QDir::NoDotAndDotDot);
    if (entries.size() != 1) return false;
    const auto original = read(data, "ui_settings.ini");
    if (!write(QDir(snapshots).filePath(entries[0]), "before.json", "{\"version\":1,\"files\":[]}")) return false;
    const bool rejected = !BackupService::restoreIfRequested(data, &restored, &error);
    return expect(rejected && !error.isEmpty(), "corrupt preimage inventory prevents account loading") &&
           expect(read(data, "ui_settings.ini") == original, "corrupt metadata is not used to delete/reset data") &&
           expect(QFileInfo::exists(QDir(data).filePath("restore_transaction.json")),
                  "failed recovery retains its journal for diagnosis");
}

bool testStartupRestoreOrdering(const QString& root)
{
    const QString source = QDir(root).filePath("source");
    const QString data = QDir(root).filePath("data");
    const QString legacy = QDir(root).filePath("legacy");
    const QString backup = QDir(root).filePath("backup");
    if (!write(source, "ui_settings.ini", "[test]\nvalue=restored\n") ||
        !write(data, "users.dat", "original-raw-data") || !write(legacy, "users.dat", "legacy")) return false;
    QString restored, error, notice;
    if (!BackupSnapshot::create(source, backup, &error)) return false;
    StartupDataService startup(data);
    if (!startup.acquire(&error) || !BackupService::requestRestore(data, backup, &error) ||
        !BackupService::restoreIfRequested(data, &restored, &error, &notice, &startup) ||
        !startup.prepare(legacy, {}, nullptr, &error)) return false;
    bool ok = expect(!QFileInfo::exists(QDir(data).filePath("users.dat")),
                     "legacy source cannot repopulate files intentionally absent after restore");
    ok &= expect(read(data, "ui_settings.ini") == "[test]\nvalue=restored\n", "restored state wins");

    const QString invalid = QDir(root).filePath("invalid-request-data");
    if (!write(invalid, "restore_request.txt", source.toUtf8())) return false;
    StartupDataService invalidStartup(invalid);
    if (!invalidStartup.acquire(&error)) return false;
    ok &= expect(!BackupService::restoreIfRequested(invalid, &restored, &error, &notice, &invalidStartup),
                 "invalid request stops before legacy import suppression");
    ok &= expect(!QFileInfo::exists(QDir(invalid).filePath("legacy_migration.json")),
                 "invalid backup does not change migration decisions");
    return ok;
}

bool testCommittedDamageStopsLoading(const QString& root)
{
    const QString source = QDir(root).filePath("source");
    const QString data = QDir(root).filePath("data");
    const QString backup = QDir(root).filePath("backup");
    if (!write(source, "ui_settings.ini", "[test]\nvalue=restored\n") ||
        !write(data, "ui_settings.ini", "[test]\nvalue=original\n")) return false;
    QString restored, error;
    if (!BackupSnapshot::create(source, backup, &error)) return false;
    RestoreTransaction::Fault fault{RestoreTransaction::Fault::Point::AfterCommit, 1};
    if (RestoreTransaction::apply(data, backup, &restored, &error, &fault)) return false;
    if (!write(data, "ui_settings.ini", "damaged-after-commit")) return false;
    return expect(!BackupService::restoreIfRequested(data, &restored, &error),
                  "committed-but-damaged restore cannot silently load account state") &&
           expect(QFileInfo::exists(QDir(data).filePath("restore_transaction.json")) && !error.isEmpty(),
                  "damaged committed restore retains recovery evidence");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--recover") {
        QString restored, error;
        const bool ok = BackupService::restoreIfRequested(app.arguments()[2], &restored, &error);
        if (!ok) std::cerr << error.toStdString() << '\n';
        return ok ? 0 : 1;
    }
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    bool ok = true;
    for (const auto point : {RestoreTransaction::Fault::Point::ApplyFailure,
                            RestoreTransaction::Fault::Point::ApplyInterruption,
                            RestoreTransaction::Fault::Point::RollbackInterruption,
                            RestoreTransaction::Fault::Point::AfterCommit}) {
        const int cuts = point == RestoreTransaction::Fault::Point::RollbackInterruption ? 5 : 2;
        for (int cut = 1; cut <= cuts; ++cut) {
            const QString root = QDir(sandbox.path()).filePath(
                QString("case-%1-%2").arg(static_cast<int>(point)).arg(cut));
            ok &= runScenario(root, point, cut);
        }
    }
    ok &= testCorruptRecoveryMetadata(QDir(sandbox.path()).filePath("corrupt-metadata"));
    ok &= testStartupRestoreOrdering(QDir(sandbox.path()).filePath("startup-order"));
    ok &= testCommittedDamageStopsLoading(QDir(sandbox.path()).filePath("committed-damage"));
    return ok ? 0 : 1;
}
