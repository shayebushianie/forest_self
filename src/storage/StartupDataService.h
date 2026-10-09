#ifndef STARTUPDATASERVICE_H
#define STARTUPDATASERVICE_H

#include "storage/ApplicationInstanceLock.h"
#include <QStringList>

// Keeps the directory lock alive throughout the application lifetime.
class StartupDataService final {
public:
    explicit StartupDataService(QString dataDirectory);
    bool acquire(QString* error = nullptr);
    bool markMigrationComplete(QString* error = nullptr);
    bool prepare(const QString& legacyDirectory, const QStringList& legacyAccountFiles,
                 QString* migratedFrom = nullptr, QString* error = nullptr);

private:
    QString dataDirectory_;
    ApplicationInstanceLock lock_;
    bool targetLockAcquired_ = false;
};

#endif // STARTUPDATASERVICE_H
