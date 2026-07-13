#ifndef APPLICATION_INSTANCE_LOCK_H
#define APPLICATION_INSTANCE_LOCK_H

#include <QLockFile>
#include <QString>

class ApplicationInstanceLock final {
public:
    explicit ApplicationInstanceLock(const QString& appDataDirectory);

    bool acquire(QString* error = nullptr);

private:
    QLockFile lock_;
};

#endif // APPLICATION_INSTANCE_LOCK_H
