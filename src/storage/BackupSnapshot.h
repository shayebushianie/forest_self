#ifndef BACKUPSNAPSHOT_H
#define BACKUPSNAPSHOT_H

#include <QStringList>

struct BackupManifest {
    QStringList files;
};

class BackupSnapshot final {
public:
    static bool create(const QString& sourceDirectory, const QString& targetDirectory,
                       QString* error = nullptr);
    static bool validate(const QString& directory, BackupManifest* manifest, QString* error = nullptr);
    static bool isManagedPath(const QString& relative);
    static bool copyFile(const QString& source, const QString& target, QString* error = nullptr);
};

#endif
