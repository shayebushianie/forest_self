#include "storage/UserManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
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

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    if (!expect(dir.isValid(), "temporary directory is available")) return 1;
    return testLegacyPasswordMigration(QDir(dir.path()).filePath("users.dat")) ? 0 : 1;
}
