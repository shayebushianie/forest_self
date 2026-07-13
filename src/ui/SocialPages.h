#ifndef SOCIAL_PAGES_H
#define SOCIAL_PAGES_H

#include <QObject>
#include <QString>

class QWidget;
class QLineEdit;
class QListWidget;
class QLabel;
class FriendManager;

class FriendsPageController final : public QObject {
    Q_OBJECT
public:
    FriendsPageController(FriendManager& friends, QWidget* parent);
    QWidget* page() const { return page_; }
    void refresh(const QString& filter = QString());

signals:
    void challengeRequested();

private:
    void refreshBrowse(const QString& filter);
    void refreshRequests();
    void refreshFriends();

    FriendManager& friends_;
    QWidget* page_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QListWidget* userList_ = nullptr;
    QListWidget* requestList_ = nullptr;
    QListWidget* friendList_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QWidget* challengeButton_ = nullptr;
};

#endif // SOCIAL_PAGES_H
