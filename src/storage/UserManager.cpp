#include "storage/UserManager.h"
#include <cstring>
#include <sstream>
#include <iomanip>

UserManager::UserManager(const std::string& filePath) : filePath_(filePath) {}

UserManager::~UserManager() { close(); }

bool UserManager::open()
{
    file_.open(filePath_, std::ios::binary | std::ios::in | std::ios::out);
    if (!file_.is_open()) {
        std::ofstream out(filePath_, std::ios::binary | std::ios::trunc);
        out.close();
        file_.open(filePath_, std::ios::binary | std::ios::in | std::ios::out);
    }
    if (file_.is_open()) {
        file_.seekg(0, std::ios::end);
        userCount_ = static_cast<uint32_t>(file_.tellg() / RECORD_SIZE);
        return true;
    }
    return false;
}

void UserManager::close()
{
    if (file_.is_open()) file_.close();
}

std::string UserManager::hashPassword(const std::string& password, const std::string& salt) const
{
    uint32_t hash = 2166136261U;
    std::string combined = password + salt + "ForestSalt2026";

    for (char c : combined) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619U;
    }

    std::stringstream ss;
    ss << std::setw(8) << std::setfill('0') << std::hex << hash;
    std::string hexStr = ss.str();
    while (hexStr.length() < 32) hexStr += "0";
    return hexStr;
}

bool UserManager::isUsernameExists(const std::string& username)
{
    file_.clear();
    file_.seekg(0, std::ios::beg);
    char buf[RECORD_SIZE];

    for (uint32_t i = 0; i < userCount_; ++i) {
        file_.read(buf, RECORD_SIZE);
        UserRecord rec;
        rec.unpack(buf);
        if (std::strcmp(rec.username, username.c_str()) == 0)
            return true;
    }
    return false;
}

bool UserManager::registerUser(const std::string& username, const std::string& password)
{
    if (username.empty() || password.empty() || username.length() >= 24)
        return false;
    if (isUsernameExists(username))
        return false;

    file_.clear();
    UserRecord rec;
    rec.userId = userCount_ + 1;
    std::strncpy(rec.username, username.c_str(), 24);

    std::string hash = hashPassword(password, username);
    std::strncpy(rec.passwordHash, hash.c_str(), 32);

    file_.seekp(userCount_ * RECORD_SIZE, std::ios::beg);
    char buf[RECORD_SIZE];
    rec.pack(buf);
    file_.write(buf, RECORD_SIZE);
    file_.flush();
    ++userCount_;
    return true;
}

bool UserManager::loginUser(const std::string& username, const std::string& password, uint32_t& outUserId)
{
    file_.clear();
    file_.seekg(0, std::ios::beg);
    char buf[RECORD_SIZE];
    std::string inputHash = hashPassword(password, username);

    for (uint32_t i = 0; i < userCount_; ++i) {
        file_.read(buf, RECORD_SIZE);
        UserRecord rec;
        rec.unpack(buf);

        if (std::strcmp(rec.username, username.c_str()) == 0) {
            if (std::strcmp(rec.passwordHash, inputHash.c_str()) == 0) {
                outUserId = rec.userId;
                return true;
            }
            break;
        }
    }
    return false;
}
