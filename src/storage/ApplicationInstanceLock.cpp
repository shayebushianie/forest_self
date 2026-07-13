#include "storage/ApplicationInstanceLock.h"

#include <QDir>

ApplicationInstanceLock::ApplicationInstanceLock(const QString& appDataDirectory)
    : lock_(QDir(appDataDirectory).filePath("forest.lock"))
{
    lock_.setStaleLockTime(30000);
}

bool ApplicationInstanceLock::acquire(QString* error)
{
    if (lock_.tryLock(0)) return true;
    if (error) {
        qint64 pid = 0;
        QString host;
        QString app;
        if (lock_.getLockInfo(&pid, &host, &app)) {
            *error = QStringLiteral("程序已在运行（进程 %1）。").arg(pid);
        } else {
            *error = QStringLiteral("无法获取应用数据目录写入锁。");
        }
    }
    return false;
}
