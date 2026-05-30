#ifndef PATHCONFIG_H
#define PATHCONFIG_H

#include <QString>
#include <QDir>
#include <QCoreApplication>

class PathConfig {
public:
    static QString getUserSandboxDir(uint32_t userId)
    {
        QString exeDir = QCoreApplication::applicationDirPath();
        QString userPath = QString("%1/app_data/user_%2").arg(exeDir).arg(userId);

        QDir dir(userPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        return userPath;
    }
};

#endif // PATHCONFIG_H
