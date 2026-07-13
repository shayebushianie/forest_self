#ifndef FRIENDDIALOG_H
#define FRIENDDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QStackedWidget>
#include <QLineEdit>

class FriendManager;
class ChallengeManager;
class DatabaseManager;

class FriendDialog : public QDialog {
    Q_OBJECT

public:
    explicit FriendDialog(FriendManager* mgr,
                          ChallengeManager* challengeMgr,
                          DatabaseManager* db,
                          QWidget* parent = nullptr);

private slots:
    void switchToBrowse();
    void switchToRequests();
    void switchToFriends();
    void onSearch();
    void sendFriendRequest();
    void acceptRequest();
    void rejectRequest();
    void openChallenge();
    void openFriendForest();

private:
    void setupUI();
    void refreshBrowsePage(const QString& filter = QString());
    void refreshRequestsPage();
    void refreshFriendsPage();

    FriendManager*     m_mgr;
    ChallengeManager*  m_challengeMgr;
    DatabaseManager*   m_db;

    QStackedWidget* m_stack;
    QPushButton*    m_browseBtn;
    QPushButton*    m_reqsBtn;
    QPushButton*    m_friendsBtn;
    QLabel*         m_reqsBadge;

    // Browse page
    QLineEdit*     m_searchEdit;
    QListWidget*   m_userList;

    // Requests page
    QListWidget*   m_reqsList;

    // Friends page
    QListWidget*   m_friendsList;
    QPushButton*   m_challengeBtn;
    QPushButton*   m_forestBtn;
};

#endif
