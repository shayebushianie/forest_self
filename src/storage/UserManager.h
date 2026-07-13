#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <string>
#include <vector>
#include <QByteArray>
#include "common/DatabaseCommon.h"

// Minimal local account store backed by fixed-size UserRecord entries.
// This is for project-local login separation, not production-grade security.
class UserManager {
public:
    explicit UserManager(const std::string& filePath);
    ~UserManager();

    bool open();
    void close();

    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password, uint32_t& outUserId);
    std::vector<std::pair<uint32_t, std::string>> getAllUsers();

private:
    std::string legacyHashPassword(const std::string& password, const std::string& salt) const;
    bool verifyPassword(const UserRecord& record, const std::string& password) const;
    void setPassword(UserRecord& record, const std::string& password) const;
    bool isUsernameExists(const std::string& username);
    bool loadLegacyUsers(const QByteArray& bytes);
    bool saveUsers() const;

    std::string filePath_;
    std::vector<UserRecord> users_;
    uint32_t userCount_ = 0;
    static constexpr size_t RECORD_SIZE = 64;
};

#endif // USERMANAGER_H
