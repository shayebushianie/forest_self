#ifndef FRIENDMANAGER_H
#define FRIENDMANAGER_H

#include <QString>
#include <QVector>
#include <cstdint>

struct FriendRequest {
    uint32_t fromUserId;
    uint32_t toUserId;
    qint64   timestamp;
    quint8   status; // 0=pending, 1=accepted, 2=rejected
};

struct FriendInfo {
    uint32_t userId;
    QString  username;
};

class UserManager;

class FriendManager {
public:
    FriendManager(const QString& reqsFilePath,
                  const QString& friendsFilePath,
                  uint32_t myUserId,
                  UserManager& userMgr);

    bool load();
    bool loadRequests();
    bool saveRequests();
    bool loadFriends();
    bool saveFriends();

    // Browse all registered users (excluding self)
    QVector<FriendInfo> allUsers() const;

    // Send friend request
    bool sendRequest(uint32_t toUserId);

    // Get pending requests TO me
    QVector<FriendRequest> incomingRequests() const;

    // Get pending requests FROM me
    QVector<FriendRequest> outgoingRequests() const;

    // Respond to a request
    bool acceptRequest(uint32_t fromUserId);
    bool rejectRequest(uint32_t fromUserId);

    // Friend list
    QVector<FriendInfo> friends() const;
    bool isFriend(uint32_t userId) const;
    int  pendingCount() const;

    uint32_t myUserId() const { return m_myUserId; }

private:
    int findRequestIndex(uint32_t fromUserId, uint32_t toUserId) const;

    QString        m_reqsFilePath;
    QString        m_friendsFilePath;
    uint32_t       m_myUserId;
    UserManager&   m_userMgr;

    QVector<FriendRequest> m_requests;
    QVector<uint32_t>      m_friendIds;
};

#endif
