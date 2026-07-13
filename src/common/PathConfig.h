#ifndef PATHCONFIG_H
#define PATHCONFIG_H

#include <QString>
#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QStandardPaths>

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
        const QString dataPath = isPortableMode()
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

    // Copies the old executable-adjacent data tree only when the target file
    // does not exist. Existing profile data always wins over a stale install.
    static bool migrateLegacyAppData(QString* migratedFrom = nullptr)
    {
        if (isPortableMode()) return true;

        const QDir source(legacyAppDataDir());
        const QDir target(getAppDataDir());
        if (!source.exists() || source.absolutePath() == target.absolutePath()) return true;

        bool copiedAny = false;
        QDirIterator it(source.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString sourceFile = it.next();
            const QString relative = source.relativeFilePath(sourceFile);
            const QString targetFile = target.filePath(relative);
            if (QFileInfo::exists(targetFile)) continue;
            if (!QDir().mkpath(QFileInfo(targetFile).dir().absolutePath()) ||
                !QFile::copy(sourceFile, targetFile)) {
                return false;
            }
            copiedAny = true;
        }
        if (copiedAny && migratedFrom) *migratedFrom = source.absolutePath();
        return true;
    }

    // Older builds wrote files next to the executable, and one broken package
    // nested app_data/app_data. Copy once into the canonical app_data folder.
    static void migrateLegacyFileToAppData(const QString& fileName)
    {
        if (isPortableMode()) return;
        QDir exeDir(QCoreApplication::applicationDirPath());
        QString legacyPath = exeDir.filePath(fileName);
        QString nestedLegacyPath = QDir(getAppDataDir()).filePath(QString("app_data/%1").arg(fileName));
        QString dataPath = getAppDataFilePath(fileName);

        if (QFileInfo::exists(dataPath)) {
            return;
        }
        if (QFileInfo::exists(legacyPath)) {
            QFile::copy(legacyPath, dataPath);
        } else if (QFileInfo::exists(nestedLegacyPath)) {
            QFile::copy(nestedLegacyPath, dataPath);
        }
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
