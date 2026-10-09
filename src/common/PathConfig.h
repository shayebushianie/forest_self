#ifndef PATHCONFIG_H
#define PATHCONFIG_H

#include <QString>
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariant>

// Centralizes runtime data paths. Installed builds use the current Windows
// user's profile; a sibling forest.portable marker opts into portable storage.
class PathConfig {
public:
    static bool isPortableMode()
    {
        return QFileInfo::exists(QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QStringLiteral("forest.portable")));
    }

    static QString getAppDataDir()
    {
        const QString testRoot = QStandardPaths::isTestModeEnabled() && QCoreApplication::instance()
            ? QCoreApplication::instance()->property("forest.testDataRoot").toString() : QString();
        const QString dataPath = QDir::isAbsolutePath(testRoot) ? testRoot : isPortableMode()
            ? QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app_data"))
            : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

        QDir dir(dataPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        return dir.absolutePath();
    }

    static QString getAppDataFilePath(const QString& fileName)
    {
        return QDir(getAppDataDir()).filePath(fileName);
    }

    static QString legacyAppDataDir()
    {
        return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("app_data"));
    }

    static QString getUserSandboxDir(uint32_t userId)
    {
        QString userPath = QDir(getAppDataDir()).filePath(QString("user_%1").arg(userId));

        QDir dir(userPath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        return userPath;
    }
};

#endif // PATHCONFIG_H
