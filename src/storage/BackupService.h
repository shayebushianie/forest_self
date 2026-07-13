#ifndef BACKUPSERVICE_H
#define BACKUPSERVICE_H

#include <QString>

class DatabaseManager;

class BackupService final {
public:
    static bool createBackup(const QString& dataDirectory, const QString& destinationRoot,
                             QString* backupDirectory, QString* error = nullptr);
    static bool requestRestore(const QString& dataDirectory, const QString& backupDirectory,
                               QString* error = nullptr);
    static bool restoreIfRequested(const QString& dataDirectory, QString* restoredFrom,
                                   QString* error = nullptr);
    static bool exportFocusCsv(DatabaseManager& database, const QString& filePath,
                               QString* error = nullptr);
};

#endif // BACKUPSERVICE_H
