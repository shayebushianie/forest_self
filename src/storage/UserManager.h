#ifndef USERMANAGER_H
#define USERMANAGER_H

#include <string>
#include <fstream>
#include "common/DatabaseCommon.h"

class UserManager {
public:
    explicit UserManager(const std::string& filePath);
    ~UserManager();

    bool open();
    void close();

    bool registerUser(const std::string& username, const std::string& password);
    bool loginUser(const std::string& username, const std::string& password, uint32_t& outUserId);

private:
    std::string hashPassword(const std::string& password, const std::string& salt) const;
    bool isUsernameExists(const std::string& username);

    std::string filePath_;
    std::fstream file_;
    uint32_t userCount_ = 0;
    static constexpr size_t RECORD_SIZE = 64;
};

#endif // USERMANAGER_H
