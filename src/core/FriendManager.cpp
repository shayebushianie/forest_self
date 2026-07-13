#include "core/FriendManager.h"
#include "storage/StorageEnvelope.h"
#include "storage/UserManager.h"

#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QDateTime>

namespace {

constexpr quint32 kRequestsMagic = 0x46525131u; // FRQ1
constexpr quint32 kFriendsMagic = 0x46465231u;  // FFR1
constexpr quint16 kFriendsVersion = 1;
constexpr qint32 kMaxFriendEntries = 100000;

bool decodeRequests(const QByteArray& bytes, QVector<FriendRequest>& requests)
{
    if (bytes.isEmpty()) { requests.clear(); return true; }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    qint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count < 0 || count > kMaxFriendEntries) return false;
    QVector<FriendRequest> loaded;
    loaded.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        FriendRequest request;
        stream >> request.fromUserId >> request.toUserId >> request.timestamp >> request.status;
        if (stream.status() != QDataStream::Ok) return false;
        loaded.append(request);
    }
    if (!buffer.atEnd()) return false;
    requests = std::move(loaded);
    return true;
}

bool decodeFriends(const QByteArray& bytes, QVector<uint32_t>& friends)
{
    if (bytes.isEmpty()) { friends.clear(); return true; }
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return false;
    QDataStream stream(&buffer);
    qint32 count = 0;
    stream >> count;
    if (stream.status() != QDataStream::Ok || count < 0 || count > kMaxFriendEntries) return false;
    QVector<uint32_t> loaded;
    loaded.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        uint32_t id = 0;
        stream >> id;
        if (stream.status() != QDataStream::Ok) return false;
        loaded.append(id);
    }
    if (!buffer.atEnd()) return false;
    friends = std::move(loaded);
    return true;
}

QByteArray encodeRequests(const QVector<FriendRequest>& requests)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<qint32>(requests.size());
    for (const FriendRequest& request : requests)
        stream << request.fromUserId << request.toUserId << request.timestamp << request.status;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

QByteArray encodeFriends(const QVector<uint32_t>& friends)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QDataStream stream(&buffer);
    stream << static_cast<qint32>(friends.size());
    for (uint32_t id : friends) stream << id;
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

} // namespace

FriendManager::FriendManager(const QString& reqsFilePath, const QString& friendsFilePath,
                             uint32_t myUserId, UserManager& userMgr)
    : m_reqsFilePath(reqsFilePath), m_friendsFilePath(friendsFilePath),
      m_myUserId(myUserId), m_userMgr(userMgr)
{
}

bool FriendManager::load()
{
    return loadRequests() && loadFriends();
}

bool FriendManager::loadRequests()
{
    const auto result = StorageEnvelope::read(m_reqsFilePath, kRequestsMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) return true;
    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_reqsFilePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kFriendsVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!decodeRequests(bytes, m_requests)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? saveRequests() : true;
}

bool FriendManager::saveRequests()
{
    return StorageEnvelope::write(m_reqsFilePath, kRequestsMagic, kFriendsVersion,
                                  encodeRequests(m_requests));
}

bool FriendManager::loadFriends()
{
    const auto result = StorageEnvelope::read(m_friendsFilePath, kFriendsMagic);
    if (result.state == StorageEnvelope::ReadState::Missing) return true;
    QByteArray bytes;
    if (result.state == StorageEnvelope::ReadState::Legacy) {
        QFile file(m_friendsFilePath);
        if (!file.open(QIODevice::ReadOnly)) return false;
        bytes = file.readAll();
        file.close();
    } else if (result.hasPayload() && result.version == kFriendsVersion) {
        bytes = result.payload;
    } else {
        return false;
    }
    if (!decodeFriends(bytes, m_friendIds)) return false;
    return result.state == StorageEnvelope::ReadState::Legacy ? saveFriends() : true;
}

bool FriendManager::saveFriends()
{
    return StorageEnvelope::write(m_friendsFilePath, kFriendsMagic, kFriendsVersion,
                                  encodeFriends(m_friendIds));
}

QVector<FriendInfo> FriendManager::allUsers() const
{
    QVector<FriendInfo> result;
    for (const auto& [id, name] : m_userMgr.getAllUsers()) {
        if (id != m_myUserId) result.append({id, QString::fromStdString(name)});
    }
    return result;
}

bool FriendManager::sendRequest(uint32_t toUserId)
{
    if (toUserId == m_myUserId || m_friendIds.contains(toUserId) ||
        findRequestIndex(m_myUserId, toUserId) >= 0) return false;
    m_requests.append({m_myUserId, toUserId, QDateTime::currentSecsSinceEpoch(), 0});
    if (saveRequests()) return true;
    m_requests.removeLast();
    return false;
}

QVector<FriendRequest> FriendManager::incomingRequests() const
{
    QVector<FriendRequest> result;
    for (const FriendRequest& request : m_requests)
        if (request.toUserId == m_myUserId && request.status == 0) result.append(request);
    return result;
}

QVector<FriendRequest> FriendManager::outgoingRequests() const
{
    QVector<FriendRequest> result;
    for (const FriendRequest& request : m_requests)
        if (request.fromUserId == m_myUserId && request.status == 0) result.append(request);
    return result;
}

bool FriendManager::acceptRequest(uint32_t fromUserId)
{
    const int requestIndex = findRequestIndex(fromUserId, m_myUserId);
    if (requestIndex < 0 || m_requests[requestIndex].status != 0) return false;
    const uint8_t oldStatus = m_requests[requestIndex].status;
    m_requests[requestIndex].status = 1;
    if (!saveRequests()) {
        m_requests[requestIndex].status = oldStatus;
        return false;
    }
    if (!m_friendIds.contains(fromUserId)) {
        m_friendIds.append(fromUserId);
        if (!saveFriends()) {
            m_friendIds.removeLast();
            m_requests[requestIndex].status = oldStatus;
            saveRequests();
            return false;
        }
    }
    return true;
}

bool FriendManager::rejectRequest(uint32_t fromUserId)
{
    const int requestIndex = findRequestIndex(fromUserId, m_myUserId);
    if (requestIndex < 0 || m_requests[requestIndex].status != 0) return false;
    const uint8_t oldStatus = m_requests[requestIndex].status;
    m_requests[requestIndex].status = 2;
    if (saveRequests()) return true;
    m_requests[requestIndex].status = oldStatus;
    return false;
}

QVector<FriendInfo> FriendManager::friends() const
{
    QVector<FriendInfo> result;
    for (const auto& [id, name] : m_userMgr.getAllUsers()) {
        if (m_friendIds.contains(id)) result.append({id, QString::fromStdString(name)});
    }
    return result;
}

bool FriendManager::isFriend(uint32_t userId) const
{
    return m_friendIds.contains(userId);
}

int FriendManager::pendingCount() const
{
    int count = 0;
    for (const FriendRequest& request : m_requests)
        if (request.toUserId == m_myUserId && request.status == 0) ++count;
    return count;
}

int FriendManager::findRequestIndex(uint32_t fromUserId, uint32_t toUserId) const
{
    for (int i = 0; i < m_requests.size(); ++i)
        if (m_requests[i].fromUserId == fromUserId && m_requests[i].toUserId == toUserId)
            return i;
    return -1;
}
