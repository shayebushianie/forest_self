#include "storage/BackupService.h"

#include <QCoreApplication>
#include "utils/Logger.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool expect(bool value, const char* message)
{
    if (!value) std::cerr << "FAILED: " << message << '\n';
    return value;
}

bool writeFile(const QString& path, const QByteArray& content)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool seedBackup(const QString& directory, int version = 1)
{
    return writeFile(QDir(directory).filePath("sample.dat"), "before") &&
           writeFile(QDir(directory).filePath("backup_manifest.txt"),
                     "ForestBackup\nversion=" + QByteArray::number(version) +
                     "\ncreated=2024-01-01T00:00:00Z\n");
}

bool testRestorePaths(const QString& root)
{
    const QString data = QDir(root).filePath("restore-data");
    const QString nested = QDir(data).filePath("nested-backup");
    if (!seedBackup(data) || !seedBackup(nested)) return false;
    QString error;
    bool ok = expect(!BackupService::requestRestore(data, data, &error), "self restore is refused");
    ok &= expect(!BackupService::requestRestore(data, nested, &error), "nested source restore is refused");
    ok &= expect(!BackupService::requestRestore(nested, data, &error), "ancestor source restore is refused");

    // Simulate a stale/manual request without using a real account directory.
    if (!writeFile(QDir(data).filePath("restore_request.txt"), data.toUtf8())) return false;
    QString restored;
    ok &= expect(!BackupService::restoreIfRequested(data, &restored, &error),
                 "execution revalidates a self-restore request");
    ok &= expect(readFile(QDir(data).filePath("sample.dat")) == "before",
                 "unsafe execution preserves the original data");
#ifdef Q_OS_WIN
    const QString junction = QDir(root).filePath("linked-data");
    QString escapedLink = junction;
    QString escapedTarget = data;
    escapedLink.replace("'", "''");
    escapedTarget.replace("'", "''");
    QProcess createLink;
    createLink.start("powershell", {"-NoProfile", "-Command",
        QString("New-Item -ItemType Junction -Path '%1' -Target '%2' | Out-Null")
            .arg(escapedLink, escapedTarget)});
    if (!createLink.waitForFinished(10000) || createLink.exitCode() != 0) {
        std::cerr << "FAILED: temporary junction fixture cannot be created\n";
        return false;
    }
    ok &= expect(!BackupService::requestRestore(data, junction, &error), "linked self restore is refused");
    QString backup;
    ok &= expect(!BackupService::createBackup(data, QDir(junction).filePath("nested"), &backup, &error),
                 "backup beneath a linked data directory is refused");
    // This removes only the junction created above, not its target directory.
    ok &= expect(QDir().rmdir(junction), "temporary junction can be removed safely");
#endif
    return ok;
}

bool testManifest(const QString& root)
{
    const QString data = QDir(root).filePath("manifest-data");
    const QString future = QDir(root).filePath("future-backup");
    const QString empty = QDir(root).filePath("empty-backup");
    QDir().mkpath(data);
    if (!seedBackup(future, 99) || !writeFile(QDir(empty).filePath("backup_manifest.txt"),
        "ForestBackup\nversion=1\ncreated=2024-01-01T00:00:00Z\n")) return false;
    QString error;
    bool ok = expect(!BackupService::requestRestore(data, future, &error), "future manifest is refused");
    ok &= expect(!BackupService::requestRestore(data, empty, &error), "manifest-only backup is refused");
    if (!writeFile(QDir(future).filePath("backup_manifest.txt"), "not a backup")) return false;
    ok &= expect(!BackupService::requestRestore(data, future, &error), "malformed manifest is refused");
    return ok;
}

bool testUniqueBackup(const QString& root)
{
    const QString data = QDir(root).filePath("unique-data");
    const QString output = QDir(root).filePath("unique-output");
    if (!writeFile(QDir(data).filePath("ui_settings.ini"), "[test]\nvalue=before\n")) return false;
    QString first, second, error;
    if (!expect(!BackupService::createBackup(data, data, &first, &error), "self backup is refused") ||
        !expect(!BackupService::createBackup(data, QDir(data).filePath("nested"), &first, &error),
                "nested backup is refused")) return false;
    if (!BackupService::createBackup(data, output, &first, &error)) return false;
    if (!writeFile(QDir(data).filePath("ui_settings.ini"), "[test]\nvalue=after\n")) return false;
    if (!BackupService::createBackup(data, output, &second, &error)) return false;
    return expect(first != second, "backups have collision-free destinations") &&
           expect(readFile(QDir(first).filePath("ui_settings.ini")) == "[test]\nvalue=before\n", "earlier backup stays intact");
}

bool testValidRestore(const QString& root)
{
    const QString data = QDir(root).filePath("valid-data");
    const QString output = QDir(root).filePath("valid-backups");
    if (!writeFile(QDir(data).filePath("ui_settings.ini"), "[test]\nvalue=snapshot\n")) return false;
    QString backup, error, restored;
    if (!BackupService::createBackup(data, output, &backup, &error) ||
        !writeFile(QDir(data).filePath("ui_settings.ini"), "[test]\nvalue=current\n")) return false;
    bool ok = expect(BackupService::requestRestore(data, backup, &error), "valid backup schedules restore");
    ok &= expect(BackupService::restoreIfRequested(data, &restored, &error), "valid restore succeeds");
    ok &= expect(restored == backup && readFile(QDir(data).filePath("ui_settings.ini")) == "[test]\nvalue=snapshot\n",
                 "restore copies requested contents");
    ok &= expect(!QFileInfo::exists(QDir(data).filePath("backup_manifest.txt")),
                 "manifest is not copied into live data");
    ok &= expect(!QFileInfo::exists(QDir(data).filePath("restore_request.txt")), "request clears after success");
    return ok;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir sandbox;
    if (!sandbox.isValid()) return 2;
    const QString logPath = QDir(sandbox.path()).filePath("backup-events.log");
    Logger::instance().setLogPath(logPath.toStdString());
    Logger::instance().setSessionId("backup-path-test");
    if (!Logger::instance().open()) return 1;
    bool ok = testRestorePaths(sandbox.path());
    ok &= testManifest(sandbox.path());
    ok &= testUniqueBackup(sandbox.path());
    ok &= testValidRestore(sandbox.path());
    Logger::instance().close();
    QFile log(logPath);
    if (!log.open(QIODevice::ReadOnly)) return 1;
    const QByteArray events = log.readAll();
    ok &= expect(events.contains("[module=backup] [operation=create]") &&
                 events.contains("[module=backup] [operation=restore-request]") &&
                 events.contains("[module=backup] [operation=restore-apply]"),
                 "backup publish, restore request and restore outcome events are logged");
    ok &= expect(!events.contains(sandbox.path().toUtf8()), "backup diagnostic events omit local paths");
    return ok ? 0 : 1;
}
