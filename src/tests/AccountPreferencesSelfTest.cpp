#include "config/UserPreferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

QString preferencesPath(const QString& directory)
{
    return QDir(directory).filePath(QStringLiteral("preferences.ini"));
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool writeFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(contents) == contents.size();
}

bool testAccountIsolation(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString userA = QDir(root).filePath(QStringLiteral("users/1"));
    const QString userB = QDir(root).filePath(QStringLiteral("users/2"));
    UserPreferences& preferences = UserPreferences::instance();

    preferences.configure(device);
    if (!expect(preferences.load(), "configure(dir) should load standalone preferences")) return false;
    UserPreferences::AccessibilityOptions accessibility;
    accessibility.fontScalePercent = 115;
    accessibility.reducedMotion = true;
    if (!expect(preferences.setAccessibilityOptions(accessibility), "device accessibility save failed")) {
        return false;
    }

    preferences.configure(device);
    preferences.bindAccount(userA, 1, {1, 2});
    if (!expect(preferences.load(), "account A preferences failed to load")) return false;
    UserPreferences::FocusSetup setup;
    setup.minutes = 45;
    setup.plantType = 3;
    setup.tagId = 17;
    setup.stopwatch = true;
    setup.autoExtend = true;
    if (!expect(preferences.setFocusSetup(setup) && preferences.setAllowPause(false) && !preferences.allowPause(),
                "account A focus preferences failed to save")) return false;

    UserPreferences::ChallengeRewardState claims;
    claims.date = QDate(2026, 10, 6);
    claims.monthKey = QStringLiteral("202610");
    claims.taskClaimed = {true, false, true, false};
    claims.checkinClaimed = {false, true, false, false, true};
    claims.monthlyClaimed = true;
    if (!expect(preferences.saveChallengeRewardState(claims), "account A challenge state failed to save")) {
        return false;
    }
    const QStringList expectedFlags = {
        QStringLiteral("challenge/checkin/20261006/checkin-1"),
        QStringLiteral("challenge/checkin/20261006/checkin-4"),
        QStringLiteral("challenge/daily/20261006/focus-minutes"),
        QStringLiteral("challenge/daily/20261006/specified-plant"),
        QStringLiteral("challenge/monthly/202610/month-focus")
    };
    if (!expect(preferences.legacyChallengeClaimKeys() == expectedFlags,
                "account A legacy claim keys do not match stable IDs")) return false;

    accessibility.highContrast = true;
    if (!expect(preferences.setAccessibilityOptions(accessibility), "shared accessibility update failed")) {
        return false;
    }
    preferences.configure(device);
    preferences.bindAccount(userB, 2, {1, 2});
    if (!expect(preferences.load(), "account B preferences failed to load")) return false;
    if (!expect(preferences.focusSetup().minutes == 25 && preferences.allowPause(),
                "account B inherited account A focus preferences")) return false;
    const auto bClaims = preferences.challengeRewardState(QDate(2026, 10, 6), 4, 5);
    if (!expect(!bClaims.taskClaimed[0] && !bClaims.checkinClaimed[1] && !bClaims.monthlyClaimed &&
                    preferences.legacyChallengeClaimKeys().isEmpty(),
                "account B inherited account A challenge state")) return false;
    if (!expect(preferences.accessibilityOptions().fontScalePercent == 115 &&
                    preferences.accessibilityOptions().reducedMotion &&
                    preferences.accessibilityOptions().highContrast,
                "device accessibility did not remain shared")) return false;

    preferences.configure(device);
    preferences.bindAccount(userA, 1, {1, 2});
    if (!expect(preferences.load() && preferences.focusSetup().minutes == 45 && !preferences.allowPause(),
                "account A preferences did not persist across account switches")) return false;
    return true;
}

bool seedLegacyPublicSettings(const QString& device)
{
    if (!QDir().mkpath(device)) return false;
    QSettings settings(preferencesPath(device), QSettings::IniFormat);
    settings.setAtomicSyncRequired(true);
    settings.setValue(QStringLiteral("schema/version"), 1);
    settings.setValue(QStringLiteral("accessibility/fontScalePercent"), 110);
    settings.setValue(QStringLiteral("focus/minutes"), 55);
    settings.setValue(QStringLiteral("focus/allowPause"), false);
    settings.setValue(QStringLiteral("challenge/daily/date"), QStringLiteral("20261006"));
    settings.setValue(QStringLiteral("challenge/daily/tasks"), QStringLiteral("1001"));
    settings.setValue(QStringLiteral("challenge/daily/checkins"), QStringLiteral("01010"));
    settings.setValue(QStringLiteral("challenge/monthly/202609/claimed"), true);
    settings.setValue(QStringLiteral("challenge/monthly/202401/month-focus"), true);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool testAmbiguousLegacyState(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString userA = QDir(root).filePath(QStringLiteral("users/1"));
    const QString userB = QDir(root).filePath(QStringLiteral("users/2"));
    if (!expect(seedLegacyPublicSettings(device), "failed to seed ambiguous legacy settings")) return false;
    const QString path = preferencesPath(device);
    const QByteArray original = readFile(path);
    UserPreferences& preferences = UserPreferences::instance();

    preferences.configure(device);
    preferences.bindAccount(userA, 1, {1, 2});
    if (!expect(preferences.load() && preferences.unresolvedLegacyRewards() &&
                    !preferences.migrationNotice().isEmpty(),
                "ambiguous public claims were not reported")) return false;
    if (!expect(preferences.focusSetup().minutes == 25 && preferences.allowPause(),
                "ambiguous focus state was copied into an account")) return false;
    const auto blocked = preferences.challengeRewardState(QDate(2026, 10, 6), 4, 5);
    if (!expect(blocked.monthlyClaimed && blocked.taskClaimed[0] && blocked.checkinClaimed[0] &&
                    !preferences.saveChallengeRewardState(blocked),
                "ambiguous claims did not block challenge claiming")) return false;
    if (!expect(preferences.legacyChallengeClaimKeys().isEmpty(),
                "ambiguous public claims were exposed as account-owned claims")) return false;

    preferences.configure(device);
    preferences.bindAccount(userB, 2, {1, 2});
    if (!expect(preferences.load() && preferences.unresolvedLegacyRewards(),
                "ambiguous public claims did not block every account")) return false;
    if (!expect(readFile(path) == original, "ambiguous public source was modified")) return false;

    QSettings restored(path, QSettings::IniFormat);
    restored.setAtomicSyncRequired(true);
    restored.setValue(QStringLiteral("legacy/ownerUserId"), 1);
    restored.sync();
    if (!expect(restored.status() == QSettings::NoError, "failed to record explicit owner in fixture")) {
        return false;
    }
    preferences.configure(device);
    preferences.bindAccount(userA, 1, {1, 2});
    if (!expect(preferences.load() && !preferences.unresolvedLegacyRewards() &&
                    preferences.legacyChallengeClaimKeys().contains(
                        QStringLiteral("challenge/daily/20261006/focus-minutes")),
                "matching explicit owner did not restore public claims to the account")) return false;
    return true;
}

bool testSingleOwnerMigration(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString userA = QDir(root).filePath(QStringLiteral("users/7"));
    const QString userB = QDir(root).filePath(QStringLiteral("users/8"));
    if (!expect(seedLegacyPublicSettings(device), "failed to seed single-owner legacy settings")) return false;
    UserPreferences& preferences = UserPreferences::instance();

    preferences.configure(device);
    preferences.bindAccount(userA, 7, {7});
    if (!expect(preferences.load() && !preferences.unresolvedLegacyRewards() &&
                    preferences.migrationNotice().contains(QStringLiteral("已删除账户")),
                "single-account legacy migration failed")) return false;
    if (!expect(preferences.focusSetup().minutes == 55 && !preferences.allowPause(),
                "single-account focus preferences were not migrated")) return false;
    const QStringList migratedFlags = preferences.legacyChallengeClaimKeys();
    const QStringList expectedFlags = {
        QStringLiteral("challenge/checkin/20261006/checkin-1"),
        QStringLiteral("challenge/checkin/20261006/checkin-3"),
        QStringLiteral("challenge/daily/20261006/focus-minutes"),
        QStringLiteral("challenge/daily/20261006/growth"),
        QStringLiteral("challenge/monthly/202401/month-focus"),
        QStringLiteral("challenge/monthly/202609/month-focus")
    };
    if (!expect(migratedFlags == expectedFlags, "migration lost or changed historical claim keys")) {
        return false;
    }

    const QByteArray originalPublicSource = readFile(preferencesPath(device));
    const QString recoveredAccount = QDir(root).filePath(QStringLiteral("recovered-users/7"));
    preferences.configure(device);
    preferences.bindAccount(recoveredAccount, 7, {7, 8});
    if (!expect(preferences.load() && preferences.focusSetup().minutes == 25 &&
                    preferences.legacyChallengeClaimKeys() == migratedFlags,
                "retained public claims were lost or focus settings were reimported after migration")) {
        return false;
    }

    preferences.configure(device);
    preferences.bindAccount(userB, 8, {7, 8});
    if (!expect(preferences.load() && !preferences.unresolvedLegacyRewards() &&
                    preferences.focusSetup().minutes == 25 &&
                    preferences.legacyChallengeClaimKeys().isEmpty(),
                "proven legacy settings were copied to another account")) return false;

    preferences.configure(device);
    preferences.bindAccount(userA, 7, {7, 8});
    if (!expect(preferences.load() && preferences.legacyChallengeClaimKeys() == migratedFlags,
                "restarting duplicated or lost migrated claim keys")) return false;
    QSettings source(preferencesPath(device), QSettings::IniFormat);
    if (!expect(source.value(QStringLiteral("legacy/accountPreferencesMigrated")).toBool() &&
                    source.value(QStringLiteral("legacy/ownerUserId")).toUInt() == 7 &&
                    readFile(preferencesPath(device)) == originalPublicSource,
                "migration did not persist its marker or altered the legacy archive")) return false;
    return true;
}

bool testMalformedLegacyRewardEvidence(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString user = QDir(root).filePath(QStringLiteral("users/1"));
    if (!QDir().mkpath(device)) return false;
    QSettings settings(preferencesPath(device), QSettings::IniFormat);
    settings.setValue(QStringLiteral("schema/version"), 1);
    settings.setValue(QStringLiteral("challenge/daily/date"), QStringLiteral("not-a-date"));
    settings.setValue(QStringLiteral("challenge/daily/tasks"), QStringLiteral("1000"));
    settings.setValue(QStringLiteral("challenge/daily/checkins"), QStringLiteral("00000"));
    settings.sync();
    if (!expect(settings.status() == QSettings::NoError, "failed to seed malformed legacy reward")) {
        return false;
    }
    const QByteArray original = readFile(preferencesPath(device));

    UserPreferences& preferences = UserPreferences::instance();
    preferences.configure(device);
    preferences.bindAccount(user, 1, {1, 2});
    if (!expect(preferences.load() && preferences.unresolvedLegacyRewards() &&
                    !preferences.migrationNotice().isEmpty(),
                "malformed claimed flags were not classified as unresolved rewards")) return false;
    const auto blocked = preferences.challengeRewardState(QDate(2026, 10, 6), 4, 5);
    return expect(blocked.taskClaimed[0] && blocked.monthlyClaimed &&
                      preferences.legacyChallengeClaimKeys().isEmpty() &&
                      readFile(preferencesPath(device)) == original,
                  "malformed claim evidence was reset, exposed as a valid key, or modified");
}

bool testFutureVersions(const QString& root)
{
    const QString futureRoot = QDir(root).filePath(QStringLiteral("future-device"));
    const QString futureUser = QDir(root).filePath(QStringLiteral("future-user"));
    if (!QDir().mkpath(futureRoot)) return false;
    const QString futureRootPath = preferencesPath(futureRoot);
    const QByteArray futureBytes("[schema]\nversion=99\n[accessibility]\nhighContrast=true\n");
    if (!expect(writeFile(futureRootPath, futureBytes), "failed to seed future device schema")) return false;

    UserPreferences& preferences = UserPreferences::instance();
    preferences.configure(futureRoot);
    preferences.bindAccount(futureUser, 9, {9});
    if (!expect(!preferences.load() && readFile(futureRootPath) == futureBytes &&
                    !QFile::exists(preferencesPath(futureUser)),
                "future device schema was overwritten or partially initialized an account")) return false;

    const QString currentRoot = QDir(root).filePath(QStringLiteral("current-device"));
    if (!QDir().mkpath(currentRoot) || !QDir().mkpath(futureUser)) return false;
    const QString currentRootPath = preferencesPath(currentRoot);
    const QByteArray currentBytes("[schema]\nversion=1\n[accessibility]\nfontScalePercent=105\n");
    const QByteArray futureAccountBytes("[schema]\nversion=2\n[focus]\nminutes=88\n");
    if (!expect(writeFile(currentRootPath, currentBytes) &&
                    writeFile(preferencesPath(futureUser), futureAccountBytes),
                "failed to seed future account schema")) return false;
    preferences.configure(currentRoot);
    preferences.bindAccount(futureUser, 9, {9});
    return expect(!preferences.load() && readFile(currentRootPath) == currentBytes &&
                      readFile(preferencesPath(futureUser)) == futureAccountBytes,
                  "future account schema caused writes to device or account preferences");
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;

    bool passed = true;
    passed &= testAccountIsolation(QDir(temporary.path()).filePath(QStringLiteral("isolation")));
    passed &= testAmbiguousLegacyState(QDir(temporary.path()).filePath(QStringLiteral("ambiguous")));
    passed &= testSingleOwnerMigration(QDir(temporary.path()).filePath(QStringLiteral("single-owner")));
    passed &= testMalformedLegacyRewardEvidence(
        QDir(temporary.path()).filePath(QStringLiteral("malformed-legacy")));
    passed &= testFutureVersions(QDir(temporary.path()).filePath(QStringLiteral("future")));
    return passed ? 0 : 1;
}
