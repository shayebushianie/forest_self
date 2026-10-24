#include "storage/UserManager.h"

#include "storage/StorageEnvelope.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        return false;
    }
    return true;
}

std::string legacyHash(const std::string& password, const std::string& username)
{
    uint32_t hash = 2166136261U;
    const std::string input = password + username + "ForestSalt2026";
    for (char value : input) {
        hash ^= static_cast<uint8_t>(value);
        hash *= 16777619U;
    }
    std::ostringstream stream;
    stream << std::setw(8) << std::setfill('0') << std::hex << hash;
    std::string result = stream.str();
    while (result.size() < 32) result += "0";
    return result;
}

bool testLegacyPasswordMigration(const QString& path)
{
    UserRecord legacy;
    legacy.userId = 1;
    std::strncpy(legacy.username, "legacy_secure", sizeof(legacy.username) - 1);
    const std::string oldHash = legacyHash("legacy_password", legacy.username);
    // This is the historical 31-byte copy behavior that must remain compatible.
    std::strncpy(legacy.passwordHash, oldHash.c_str(), sizeof(legacy.passwordHash) - 1);
    {
        std::ofstream output(path.toStdString(), std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(&legacy), sizeof(legacy));
    }

    uint32_t userId = 0;
    UserManager users(path.toStdString());
    if (!expect(users.open(), "legacy password store loads")) return false;
    if (!expect(users.loginUser("legacy_secure", "legacy_password", userId) && userId == 1,
                "legacy password authenticates and upgrades")) return false;
    if (!expect(users.registerUser("new_secure", "new_password"), "new password account persists")) return false;

    UserManager reloaded(path.toStdString());
    uint32_t reloadedId = 0;
    return expect(reloaded.open(), "upgraded password store reloads") &&
           expect(reloaded.loginUser("legacy_secure", "legacy_password", reloadedId) && reloadedId == 1,
                  "upgraded legacy password remains valid") &&
           expect(reloaded.loginUser("new_secure", "new_password", reloadedId),
                  "new password persists securely") &&
           expect(!reloaded.loginUser("new_secure", "wrong_password", reloadedId),
                  "wrong password is rejected");
}

bool writeAccounts(const QString& path, const std::vector<UserRecord>& records)
{
    return StorageEnvelope::write(path, 0x46555331u, 2,
        QByteArray(reinterpret_cast<const char*>(records.data()),
                   static_cast<qsizetype>(records.size() * sizeof(UserRecord))));
}

UserRecord account(uint32_t id, const char* name)
{
    UserRecord record;
    record.userId = id;
    std::strncpy(record.username, name, sizeof(record.username) - 1);
    return record;
}

bool testInvalidAccounts(const QString& root)
{
    const auto first = account(1, "first");
    auto unterminated = account(2, "second");
    std::memset(unterminated.username, 'x', sizeof(unterminated.username));
    auto unknownPassword = account(2, "future-password");
    unknownPassword.reserved[0] = 99;
    const std::vector<std::vector<UserRecord>> fixtures = {
        {first, unterminated}, {first, account(1, "second")},
        {first, account(2, "first")}, {first, account(0, "second")},
        {first, account(2, "")}, {first, unknownPassword}
    };
    bool ok = true;
    for (size_t i = 0; i < fixtures.size(); ++i) {
        const auto path = QDir(root).filePath(QStringLiteral("invalid-%1.dat").arg(i));
        if (!writeAccounts(path, fixtures[i])) return false;
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return false;
        const auto before = input.readAll();
        input.close();
        UserManager users(path.toStdString());
        ok &= expect(!users.open(), "malformed or duplicate account identity is rejected");
        ok &= expect(users.getAllUsers().empty(), "failed load exposes no partial account list");
        ok &= expect(!users.registerUser("unsafe-write", "fixture-only"),
                     "failed load cannot overwrite the rejected account store");
        if (!input.open(QIODevice::ReadOnly)) return false;
        ok &= expect(input.readAll() == before, "rejected account file remains unchanged");
    }
    return ok;
}

bool testAccountIdAllocation(const QString& root)
{
    const auto path = QDir(root).filePath("noncontiguous.dat");
    if (!writeAccounts(path, {account(1, "first"), account(3, "third")})) return false;
    UserManager users(path.toStdString());
    if (!expect(users.open(), "noncontiguous valid IDs load") ||
        !expect(users.registerUser("fourth", "fixture-only"), "account after gap registers")) return false;
    const auto all = users.getAllUsers();
    bool ok = expect(all.size() == 3 && all.back().first == 4,
                     "new account uses next unused ID rather than record count");
    ok &= expect(!users.registerUser(std::string("alias\0hidden", 12), "fixture-only"),
                 "embedded null cannot create a different visible identity");
    const auto exhaustedPath = QDir(root).filePath("exhausted.dat");
    if (!writeAccounts(exhaustedPath, {account(std::numeric_limits<uint32_t>::max(), "last")})) return false;
    UserManager exhausted(exhaustedPath.toStdString());
    return expect(exhausted.open(), "maximum existing account ID stays readable") &&
           expect(!exhausted.registerUser("overflow", "fixture-only"), "account ID cannot wrap to zero") && ok;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!expect(dir.isValid(), "temporary directory is available")) return 1;
    bool ok = testInvalidAccounts(dir.path());
    ok &= testAccountIdAllocation(dir.path());
    ok &= testLegacyPasswordMigration(QDir(dir.path()).filePath("users.dat"));
    return ok ? 0 : 1;
}
