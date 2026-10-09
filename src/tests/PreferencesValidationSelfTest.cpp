#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include "system/RuleEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool seed(const QString& path, const QString& key, const QVariant& value)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("schema/version"), 1);
    settings.setValue(key, value);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

bool invalidFields(const QString& root)
{
    const QList<QPair<QString, QVariant>> cases = {
        {QStringLiteral("focus/minutes"), QStringLiteral("nonsense")},
        {QStringLiteral("focus/minutes"), 9},
        {QStringLiteral("focus/minutes"), 121},
        {QStringLiteral("focus/plantType"), 999},
        {QStringLiteral("focus/tagId"), QStringLiteral("-1")},
        {QStringLiteral("focus/tagId"), QStringLiteral("4294967296")},
        {QStringLiteral("focus/allowPause"), QStringLiteral("perhaps")},
        {QStringLiteral("accessibility/fontScalePercent"), 126},
        {QStringLiteral("accessibility/reducedMotion"), QStringLiteral("maybe")},
        {QStringLiteral("challenge/daily/tasks"), QStringLiteral("100x")},
        {QStringLiteral("challenge/daily/checkins"), QStringLiteral("20000")},
        {QStringLiteral("challenge/monthly/202610/claimed"), QStringLiteral("maybe")},
        {QStringLiteral("challenge/daily/20261006/focus-session"), QStringLiteral("maybe")},
        {QStringLiteral("supervision/blacklist"), QStringList{QStringLiteral("C:/private/browser.exe")}}
    };
    auto& preferences = UserPreferences::instance();
    for (int index = 0; index < cases.size(); ++index) {
        const QString device = QDir(root).filePath(QString::number(index));
        const QString path = QDir(device).filePath(QStringLiteral("preferences.ini"));
        if (!seed(path, cases[index].first, cases[index].second)) return false;
        const QByteArray original = read(path);
        preferences.configure(device);
        if (!expect(!preferences.load() && preferences.lastError().contains(cases[index].first) &&
                        read(path) == original, "invalid field was silently coerced or changed")) return false;
    }
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString account = QDir(root).filePath(QStringLiteral("account"));
    const QString path = QDir(device).filePath(QStringLiteral("preferences.ini"));
    if (!seed(path, QStringLiteral("accessibility/highContrast"), QStringLiteral("bad"))) return false;
    preferences.configure(device);
    preferences.bindAccount(account, 1, {1});
    return expect(!preferences.load() && !QFile::exists(QDir(account).filePath("preferences.ini")),
                  "invalid device preferences partially initialized an account");
}

bool mutationProtection(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString account = QDir(root).filePath(QStringLiteral("account"));
    const QString path = QDir(account).filePath(QStringLiteral("preferences.ini"));
    auto& preferences = UserPreferences::instance();
    preferences.configure(device);
    preferences.bindAccount(account, 1, {1});
    if (!preferences.load() || !preferences.setAllowPause(false)) return false;
    const auto initial = preferences.focusSetup();
    auto next = initial;
    next.plantType = PlantCatalog::all().back().type;
    next.minutes = 65;
    if (!seed(path, QStringLiteral("schema/version"), 99)) return false;
    const QByteArray future = read(path);
    UserPreferences::ChallengeRewardState claims;
    claims.date = QDate(2026, 10, 6);
    claims.monthKey = QStringLiteral("202610");
    claims.taskClaimed = {true};
    if (!expect(!preferences.setAllowPause(true) && !preferences.setFocusSetup(next) &&
                    !preferences.setProcessBlacklist({QStringLiteral("editor.exe")}) &&
                    !preferences.saveChallengeRewardState(claims) && read(path) == future &&
                    !preferences.allowPause() && preferences.focusSetup().minutes == initial.minutes,
                "save after load overwrote a future version or changed memory")) return false;

    const QString devicePath = QDir(device).filePath(QStringLiteral("preferences.ini"));
    if (!seed(devicePath, QStringLiteral("schema/version"), 99)) return false;
    const QByteArray futureDevice = read(devicePath);
    auto accessibility = preferences.accessibilityOptions();
    accessibility.highContrast = true;
    if (!expect(!preferences.setAccessibilityOptions(accessibility) &&
                    !preferences.accessibilityOptions().highContrast && read(devicePath) == futureDevice,
                "appearance save overwrote a future version")) return false;

    const QString failureRoot = QDir(root).filePath(QStringLiteral("failed-save"));
    preferences.configure(failureRoot);
    if (!preferences.load()) return false;
    const QString failurePath = QDir(failureRoot).filePath(QStringLiteral("preferences.ini"));
    const QString retained = failurePath + QStringLiteral(".retained");
    if (!QFile::rename(failurePath, retained) || !QDir().mkdir(failurePath)) return false;
    const bool rejected = !preferences.setFocusSetup(next) && !preferences.setAllowPause(false) &&
        !preferences.setAccessibilityOptions(accessibility) &&
        !preferences.setProcessBlacklist({QStringLiteral("editor.exe")});
    const bool unchanged = preferences.focusSetup().minutes == 25 && preferences.allowPause() &&
        !preferences.accessibilityOptions().highContrast &&
        !preferences.processBlacklist().contains(QStringLiteral("editor.exe"));
    return expect(rejected && unchanged && !preferences.lastError().isEmpty() && QFile::exists(retained),
                  "failed save changed loaded preferences or reported success");
}

bool blacklistRoundTrip(const QString& root)
{
    const QString device = QDir(root).filePath(QStringLiteral("device"));
    const QString account = QDir(root).filePath(QStringLiteral("account"));
    auto& preferences = UserPreferences::instance();
    preferences.configure(device);
    preferences.bindAccount(account, 1, {1, 2});
    if (!preferences.load()) return false;
    if (!expect(preferences.setProcessBlacklist({QStringLiteral(" Editor.EXE "), QStringLiteral("editor.exe")}) &&
                    preferences.processBlacklist() == QStringList{QStringLiteral("editor.exe")},
                "blacklist did not trim or deduplicate process identities")) return false;
    const QString path = QDir(account).filePath(QStringLiteral("preferences.ini"));
    const QByteArray previous = read(path);
    if (!expect(QProcess::execute(QCoreApplication::applicationFilePath(),
                                  {QStringLiteral("--verify-blacklist"), root, QStringLiteral("editor.exe")}) == 0,
                "blacklist failed an independent-process reload")) return false;
    if (!expect(!preferences.setProcessBlacklist({QStringLiteral("../bad.exe")}) && read(path) == previous,
                "blacklist accepted a path or changed the file after invalid input")) return false;
    auto setup = preferences.focusSetup();
    setup.plantType = 999;
    if (!expect(!preferences.setFocusSetup(setup) && read(path) == previous,
                "unknown plant was clamped to a different identity")) return false;
    UserPreferences::ChallengeRewardState invalidClaims;
    if (!expect(!preferences.saveChallengeRewardState(invalidClaims) && read(path) == previous,
                "invalid claim date was persisted")) return false;

    preferences.configure(device);
    preferences.bindAccount(account, 1, {1, 2});
    if (!preferences.load()) return false;
    RuleEngine rules;
    rules.setBlacklist(preferences.processBlacklist());
    if (!expect(rules.isViolation(QStringLiteral("EDITOR.exe")) && !rules.isViolation(QStringLiteral("chrome.exe")),
                "persisted blacklist differs from the supervision engine")) return false;
    if (!preferences.setProcessBlacklist({})) return false;
    if (!expect(QProcess::execute(QCoreApplication::applicationFilePath(),
                                  {QStringLiteral("--verify-blacklist"), root, QString()}) == 0,
                "empty blacklist failed an independent-process reload")) return false;
    preferences.configure(device);
    preferences.bindAccount(account, 1, {1, 2});
    if (!expect(preferences.load() && preferences.processBlacklist().isEmpty(),
                "empty blacklist was replaced by defaults on restart")) return false;
    preferences.configure(device);
    preferences.bindAccount(QDir(root).filePath(QStringLiteral("account-b")), 2, {1, 2});
    return expect(preferences.load() && preferences.processBlacklist().contains(QStringLiteral("chrome.exe")),
                  "blacklist leaked across accounts");
}

}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 4 && app.arguments().at(1) == QStringLiteral("--verify-blacklist")) {
        const QString root = app.arguments().at(2);
        auto& preferences = UserPreferences::instance();
        preferences.configure(QDir(root).filePath(QStringLiteral("device")));
        preferences.bindAccount(QDir(root).filePath(QStringLiteral("account")), 1, {1, 2});
        const QString expected = app.arguments().at(3);
        return preferences.load() && preferences.processBlacklist() ==
            (expected.isEmpty() ? QStringList() : QStringList{expected}) ? 0 : 1;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    bool passed = invalidFields(QDir(temporary.path()).filePath(QStringLiteral("fields")));
    passed &= mutationProtection(QDir(temporary.path()).filePath(QStringLiteral("mutations")));
    passed &= blacklistRoundTrip(QDir(temporary.path()).filePath(QStringLiteral("rules")));
    return passed ? 0 : 1;
}
