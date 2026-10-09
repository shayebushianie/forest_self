#include "core/FriendManager.h"
#include "storage/StorageEnvelope.h"
#include "storage/UserManager.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool expect(bool condition, const char* message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool readBytes(const QString& path, QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    bytes = file.readAll();
    return true;
}

bool hasFriend(const FriendManager& manager, uint32_t userId)
{
    for (const FriendInfo& friendInfo : manager.friends())
        if (friendInfo.userId == userId) return true;
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!expect(temporary.isValid(), "temporary test directory is available")) return 2;

    const QString root = temporary.path();
    UserManager users(QDir(root).filePath(QStringLiteral("users.dat")).toStdString());
    if (!expect(users.open(), "local account store opens") ||
        !expect(users.registerUser("local-alpha", "test-only-alpha"), "first local account registers") ||
        !expect(users.registerUser("local-beta", "test-only-beta"), "second local account registers")) return 2;
    const auto accounts = users.getAllUsers();
    if (!expect(accounts.size() == 2, "test uses exactly two local accounts")) return 2;

    const uint32_t alphaId = accounts[0].first;
    const uint32_t betaId = accounts[1].first;
    const QString requestsPath = QDir(root).filePath(QStringLiteral("friend_reqs.dat"));
    const QString alphaDir = QDir(root).filePath(QStringLiteral("user_%1").arg(alphaId));
    const QString betaDir = QDir(root).filePath(QStringLiteral("user_%1").arg(betaId));
    if (!QDir().mkpath(alphaDir) || !QDir().mkpath(betaDir)) return 2;
    const QString betaFriendsPath = QDir(betaDir).filePath(QStringLiteral("friends.dat"));
    QFile legacyFriendFile(betaFriendsPath);
    if (!legacyFriendFile.open(QIODevice::WriteOnly)) return 2;
    QDataStream legacyFriendStream(&legacyFriendFile);
    legacyFriendStream << qint32(1) << quint32(999999);
    if (legacyFriendStream.status() != QDataStream::Ok) return 2;
    legacyFriendFile.close();

    FriendManager alpha(requestsPath, QDir(alphaDir).filePath(QStringLiteral("friends.dat")), alphaId, users);
    FriendManager beta(requestsPath, betaFriendsPath, betaId, users);
    if (!expect(alpha.load() && beta.load(), "both account-scoped friend stores load")) return 2;
    QByteArray oldFriendsAfterLoad;
    if (!readBytes(betaFriendsPath, oldFriendsAfterLoad)) return 2;

    bool ok = true;
    ok &= expect(!alpha.sendRequest(0) && !alpha.sendRequest(999998) && !alpha.sendRequest(alphaId),
                 "invalid and self targets never create a local request");
    ok &= expect(beta.isFriend(999999), "existing account-scoped friend entry is preserved");
    ok &= expect(alpha.sendRequest(betaId), "first request is sent");
    ok &= expect(!alpha.sendRequest(betaId), "duplicate pending request is rejected");
    ok &= expect(alpha.outgoingRequests().size() == 1, "duplicate send does not add a request row");
    ok &= expect(beta.load() && beta.incomingRequests().size() == 1, "second account sees the incoming request");
    ok &= expect(!beta.sendRequest(alphaId), "reciprocal duplicate pending request is rejected");
    StorageEnvelope::failNextWriteForTesting(requestsPath);
    ok &= expect(!beta.rejectRequest(alphaId) && beta.incomingRequests().size() == 1,
                 "failed rejection preserves the pending request");
    ok &= expect(beta.rejectRequest(alphaId), "second account can reject the request");

    ok &= expect(alpha.load(), "first account reloads the rejected request");
    StorageEnvelope::failNextWriteForTesting(requestsPath);
    ok &= expect(!alpha.sendRequest(betaId) && alpha.outgoingRequests().isEmpty(),
                 "failed reapplication preserves the rejected state");
    ok &= expect(alpha.sendRequest(betaId), "rejected request can be reapplied");
    ok &= expect(!alpha.sendRequest(betaId), "reapplied pending request still rejects duplicates");
    ok &= expect(alpha.outgoingRequests().size() == 1, "reapplication reuses the existing request row");
    ok &= expect(beta.load() && beta.incomingRequests().size() == 1, "second account reloads one pending request");

    QByteArray beforeFailedAccept;
    if (!readBytes(requestsPath, beforeFailedAccept)) return 2;
    StorageEnvelope::failNextWriteForTesting(requestsPath);
    const bool acceptedDuringFailure = beta.acceptRequest(alphaId);
    StorageEnvelope::clearWriteFailuresForTesting();
    QByteArray afterFailedAccept;
    if (!readBytes(requestsPath, afterFailedAccept)) return 2;
    ok &= expect(!acceptedDuringFailure, "failed atomic save rejects the accept operation");
    ok &= expect(beforeFailedAccept == afterFailedAccept, "failed save leaves the request file unchanged");
    QByteArray friendsAfterFailedAccept;
    if (!readBytes(betaFriendsPath, friendsAfterFailedAccept)) return 2;
    ok &= expect(friendsAfterFailedAccept == oldFriendsAfterLoad,
                 "failed accept leaves existing friend-file entries untouched");
    ok &= expect(!beta.isFriend(alphaId) && !hasFriend(beta, alphaId),
                 "failed accept does not create a one-sided friend relation");
    ok &= expect(alpha.load() && beta.load(), "both accounts reload after the failed save");
    ok &= expect(alpha.outgoingRequests().size() == 1 && beta.incomingRequests().size() == 1,
                 "reload retains the pending request after failed accept");
    ok &= expect(!alpha.isFriend(betaId) && !beta.isFriend(alphaId),
                 "neither account reports friendship after failed accept");

    ok &= expect(beta.acceptRequest(alphaId), "retry accepts the same pending request");
    ok &= expect(beta.isFriend(alphaId) && hasFriend(beta, alphaId),
                 "accepting account sees the accepted relationship");
    ok &= expect(alpha.load(), "other account reloads the accepted relationship");
    ok &= expect(alpha.isFriend(betaId) && hasFriend(alpha, betaId),
                 "both local accounts derive friendship from the accepted request");
    ok &= expect(beta.load() && beta.isFriend(999999) && beta.isFriend(alphaId),
                 "reload retains legacy friend entries and the accepted relationship");
    QByteArray friendsAfterAccept;
    if (!readBytes(betaFriendsPath, friendsAfterAccept)) return 2;
    ok &= expect(friendsAfterAccept == oldFriendsAfterLoad,
                 "accept does not replace existing account-scoped friend records");
    ok &= expect(!alpha.sendRequest(betaId), "accepted relationship blocks duplicate requests");

    return ok ? 0 : 1;
}
