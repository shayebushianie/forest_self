#ifndef SOCIAL_PAGES_H
#define SOCIAL_PAGES_H

#include <QObject>
#include <QString>

class QWidget;
class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;
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
    void refreshActions();

    FriendManager& friends_;
    QWidget* page_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QListWidget* userList_ = nullptr;
    QListWidget* requestList_ = nullptr;
    QListWidget* friendList_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QLabel* browseEmpty_ = nullptr;
    QLabel* requestsEmpty_ = nullptr;
    QLabel* friendsEmpty_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
    QPushButton* rejectButton_ = nullptr;
    QPushButton* challengeButton_ = nullptr;
};

#endif // SOCIAL_PAGES_H
