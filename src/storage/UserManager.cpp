#include "storage/UserManager.h"
#include "storage/StorageEnvelope.h"

#include <QCryptographicHash>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QString>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace {

constexpr quint32 kUserMagic = 0x46555331u; // FUS1
constexpr quint16 kUserVersion = 2;
constexpr uint8_t kPasswordVersion = 2;
constexpr int kPasswordSaltBytes = 16;
constexpr int kPasswordKeyBytes = 16;
constexpr int kPasswordIterations = 100000;
constexpr size_t kUsernameBytes = 24;

QString storagePath(const std::string& path)
{
    return QString::fromStdString(path);
}

QByteArray hmacSha256(const QByteArray& message, const QByteArray& key)
{
    return QMessageAuthenticationCode::hash(message, key, QCryptographicHash::Sha256);
}

QByteArray derivePasswordKey(const std::string& password, const QByteArray& salt)
{
    const QByteArray key = QByteArray::fromStdString(password);
    QByteArray output;
    for (quint32 block = 1; output.size() < kPasswordKeyBytes; ++block) {
        QByteArray input = salt;
        input.append(static_cast<char>((block >> 24) & 0xFF));
        input.append(static_cast<char>((block >> 16) & 0xFF));
        input.append(static_cast<char>((block >> 8) & 0xFF));
        input.append(static_cast<char>(block & 0xFF));
        QByteArray current = hmacSha256(input, key);
        QByteArray mixed = current;
        for (int iteration = 1; iteration < kPasswordIterations; ++iteration) {
            current = hmacSha256(current, key);
            for (int i = 0; i < mixed.size(); ++i) mixed[i] = mixed[i] ^ current[i];
        }
        output.append(mixed);
    }
    return output.left(kPasswordKeyBytes);
}

bool constantTimeEquals(const QByteArray& left, const QByteArray& right)
{
    if (left.size() != right.size()) return false;
    uint8_t difference = 0;
    for (int i = 0; i < left.size(); ++i) {
        difference |= static_cast<uint8_t>(left.at(i)) ^ static_cast<uint8_t>(right.at(i));
    }
    return difference == 0;
}

}

UserManager::UserManager(const std::string& filePath) : filePath_(filePath) {}
UserManager::~UserManager() { close(); }

bool UserManager::open()
{
    users_.clear();
    const auto result = StorageEnvelope::read(storagePath(filePath_), kUserMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) {
        userCount_ = 0;
        return saveUsers();
    }

    QByteArray bytes;
    const bool migrateEnvelope = result.state == StorageEnvelope::ReadState::Legacy || result.version == 1;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile legacy(storagePath(filePath_));
        if (!legacy.open(QIODevice::ReadOnly)) return false;
        bytes = legacy.readAll();
    } else if (result.hasPayload() && (result.version == 1 || result.version == kUserVersion)) {
        bytes = result.payload;
    } else {
        return false;
    }

    if (!loadLegacyUsers(bytes)) return false;
    return migrateEnvelope ? saveUsers() : true;
}

void UserManager::close()
{
    users_.clear();
    userCount_ = 0;
}

std::string UserManager::legacyHashPassword(const std::string& password, const std::string& salt) const
{
    uint32_t hash = 2166136261U;
    const std::string combined = password + salt + "ForestSalt2026";
    for (char c : combined) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619U;
    }

    std::stringstream stream;
    stream << std::setw(8) << std::setfill('0') << std::hex << hash;
    std::string value = stream.str();
    while (value.length() < 32) value += "0";
    return value;
}

bool UserManager::verifyPassword(const UserRecord& record, const std::string& password) const
{
    if (static_cast<uint8_t>(record.reserved[0]) != kPasswordVersion) {
        const std::string expected = legacyHashPassword(password, record.username);
        size_t storedLength = 0;
        while (storedLength < sizeof(record.passwordHash) && record.passwordHash[storedLength] != '\0') {
            ++storedLength;
        }

        // Older builds copied only 31 characters into the 32-byte field. Both
        // representations are valid legacy credentials and are upgraded on login.
        if (storedLength != expected.size() && storedLength != expected.size() - 1) return false;
        return std::memcmp(record.passwordHash, expected.data(), storedLength) == 0;
    }
    const QByteArray stored(record.passwordHash, sizeof(record.passwordHash));
    const QByteArray salt = stored.left(kPasswordSaltBytes);
    return constantTimeEquals(stored.mid(kPasswordSaltBytes, kPasswordKeyBytes),
                              derivePasswordKey(password, salt));
}

void UserManager::setPassword(UserRecord& record, const std::string& password) const
{
    QByteArray salt(kPasswordSaltBytes, '\0');
    for (int i = 0; i < salt.size(); i += static_cast<int>(sizeof(quint32))) {
        const quint32 random = QRandomGenerator::system()->generate();
        std::memcpy(salt.data() + i, &random, std::min<int>(sizeof(random), salt.size() - i));
    }
    const QByteArray encoded = salt + derivePasswordKey(password, salt);
    std::memset(record.passwordHash, 0, sizeof(record.passwordHash));
    std::memcpy(record.passwordHash, encoded.constData(), std::min<qsizetype>(encoded.size(), sizeof(record.passwordHash)));
    std::memset(record.reserved, 0, sizeof(record.reserved));
    record.reserved[0] = static_cast<char>(kPasswordVersion);
}

bool UserManager::isUsernameExists(const std::string& username)
{
    for (const UserRecord& record : users_) {
        if (std::strcmp(record.username, username.c_str()) == 0) return true;
    }
    return false;
}

bool UserManager::registerUser(const std::string& username, const std::string& password)
{
    if (username.empty() || password.empty() || username.length() >= kUsernameBytes) return false;
    if (isUsernameExists(username)) return false;

    UserRecord record;
    record.userId = userCount_ + 1;
    std::strncpy(record.username, username.c_str(), sizeof(record.username) - 1);
    setPassword(record, password);
    users_.push_back(record);
    if (!saveUsers()) {
        users_.pop_back();
        return false;
    }
    ++userCount_;
    return true;
}

bool UserManager::loginUser(const std::string& username, const std::string& password, uint32_t& outUserId)
{
    for (UserRecord& record : users_) {
        if (std::strcmp(record.username, username.c_str()) != 0) continue;
        if (!verifyPassword(record, password)) return false;
        if (static_cast<uint8_t>(record.reserved[0]) != kPasswordVersion) {
            const UserRecord previous = record;
            setPassword(record, password);
            if (!saveUsers()) {
                record = previous;
                return false;
            }
        }
        outUserId = record.userId;
        return true;
    }
    return false;
}

std::vector<std::pair<uint32_t, std::string>> UserManager::getAllUsers()
{
    std::vector<std::pair<uint32_t, std::string>> users;
    users.reserve(users_.size());
    for (const UserRecord& record : users_) {
        users.emplace_back(record.userId, std::string(record.username));
    }
    return users;
}

bool UserManager::loadLegacyUsers(const QByteArray& bytes)
{
    if (bytes.size() % static_cast<qsizetype>(RECORD_SIZE) != 0) return false;
    users_.clear();
    users_.reserve(static_cast<size_t>(bytes.size() / static_cast<qsizetype>(RECORD_SIZE)));
    for (qsizetype offset = 0; offset < bytes.size(); offset += RECORD_SIZE) {
        UserRecord record;
        std::memcpy(&record, bytes.constData() + offset, RECORD_SIZE);
        users_.push_back(record);
    }
    userCount_ = static_cast<uint32_t>(users_.size());
    return true;
}

bool UserManager::saveUsers() const
{
    QByteArray payload;
    payload.resize(static_cast<qsizetype>(users_.size() * RECORD_SIZE));
    if (!users_.empty()) {
        std::memcpy(payload.data(), users_.data(), static_cast<size_t>(payload.size()));
    }
    return StorageEnvelope::write(storagePath(filePath_), kUserMagic, kUserVersion, payload);
}
