#include "FriendDialog.h"
#include "core/FriendManager.h"
#include "core/ChallengeManager.h"
#include "ui/ChallengeDialog.h"
#include "ui/DialogPresenter.h"
#include "storage/DatabaseManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

FriendDialog::FriendDialog(FriendManager* mgr,
                           ChallengeManager* challengeMgr,
                           DatabaseManager* db,
                           QWidget* parent)
    : QDialog(parent), m_mgr(mgr), m_challengeMgr(challengeMgr), m_db(db)
{
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("👥 用户与好友"));
    resize(520, 520);
    setupUI();
    refreshBrowsePage();
    refreshRequestsPage();
    refreshFriendsPage();
}

void FriendDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    // Tab bar
    auto* tabRow = new QHBoxLayout;

    m_browseBtn = new QPushButton(QStringLiteral("🔍 查找用户"));
    m_reqsBtn   = new QPushButton(QStringLiteral("📩 好友申请"));
    m_friendsBtn = new QPushButton(QStringLiteral("👥 我的好友"));

    QString tabStyle =
        "QPushButton { background:#e8f0e8; border:1px solid #a0c0a0;"
        " border-radius:6px; padding:6px 14px; font-weight:bold; font-size:12px; }"
        "QPushButton:hover { background:#c8e6c9; }"
        "QPushButton:checked { background:#4caf50; color:white; border-color:#4caf50; }";
    m_browseBtn->setStyleSheet(tabStyle);
    m_reqsBtn->setStyleSheet(tabStyle);
    m_friendsBtn->setStyleSheet(tabStyle);
    m_browseBtn->setCheckable(true);
    m_reqsBtn->setCheckable(true);
    m_friendsBtn->setCheckable(true);

    m_reqsBadge = new QLabel;
    m_reqsBadge->setStyleSheet("color:red; font-weight:bold; font-size:11px;");

    auto* reqsRow = new QHBoxLayout;
    reqsRow->addWidget(m_reqsBtn);
    reqsRow->addWidget(m_reqsBadge);

    tabRow->addWidget(m_browseBtn);
    tabRow->addLayout(reqsRow);
    tabRow->addWidget(m_friendsBtn);
    tabRow->addStretch();
    mainLayout->addLayout(tabRow);

    // Stack
    m_stack = new QStackedWidget;

    // Page 0: Browse users
    auto* browsePage = new QWidget;
    auto* browseLayout = new QVBoxLayout(browsePage);
    browseLayout->setContentsMargins(0, 0, 0, 0);

    auto* searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索用户名..."));
    m_searchEdit->setStyleSheet("border:1px solid #ccc; border-radius:4px; padding:4px 8px;");
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"));
    searchBtn->setStyleSheet(
        "QPushButton { background:#4caf50; color:white; border:none;"
        " border-radius:4px; padding:4px 14px; font-weight:bold; }"
        "QPushButton:hover { background:#45a049; }");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(searchBtn);
    browseLayout->addLayout(searchRow);

    m_userList = new QListWidget;
    m_userList->setStyleSheet(
        "QListWidget { border:1px solid #a0c0a0; border-radius:4px; }"
        "QListWidget::item { padding:8px; border-bottom:1px solid #eef4ee; }"
        "QListWidget::item:selected { background:#c8e6c9; }");
    browseLayout->addWidget(m_userList, 1);

    auto* sendBtn = new QPushButton(QStringLiteral("➕ 发送好友申请"));
    sendBtn->setStyleSheet(
        "QPushButton { background:#2196f3; color:white; border:none;"
        " border-radius:6px; padding:8px; font-weight:bold; font-size:13px; }"
        "QPushButton:hover { background:#1976d2; }"
        "QPushButton:disabled { background:#bbb; }");
    sendBtn->setObjectName("sendFriendBtn");
    browseLayout->addWidget(sendBtn);

    m_stack->addWidget(browsePage);

    // Page 1: Requests
    auto* reqsPage = new QWidget;
    auto* reqsLayout = new QVBoxLayout(reqsPage);
    reqsLayout->setContentsMargins(0, 0, 0, 0);

    m_reqsList = new QListWidget;
    m_reqsList->setStyleSheet(
        "QListWidget { border:1px solid #a0c0a0; border-radius:4px; }"
        "QListWidget::item { padding:8px; border-bottom:1px solid #eef4ee; }");
    reqsLayout->addWidget(m_reqsList, 1);

    auto* reqBtnRow = new QHBoxLayout;
    auto* acceptBtn = new QPushButton(QStringLiteral("✅ 同意"));
    acceptBtn->setStyleSheet(
        "QPushButton { background:#4caf50; color:white; border:none;"
        " border-radius:6px; padding:6px 20px; font-weight:bold; }"
        "QPushButton:hover { background:#45a049; }");
    acceptBtn->setObjectName("acceptBtn");
    auto* rejectBtn = new QPushButton(QStringLiteral("❌ 拒绝"));
    rejectBtn->setStyleSheet(
        "QPushButton { background:#f44336; color:white; border:none;"
        " border-radius:6px; padding:6px 20px; font-weight:bold; }"
        "QPushButton:hover { background:#d32f2f; }");
    rejectBtn->setObjectName("rejectBtn");
    reqBtnRow->addStretch();
    reqBtnRow->addWidget(acceptBtn);
    reqBtnRow->addWidget(rejectBtn);
    reqsLayout->addLayout(reqBtnRow);

    m_stack->addWidget(reqsPage);

    // Page 2: Friends list
    auto* friendsPage = new QWidget;
    auto* friendsLayout = new QVBoxLayout(friendsPage);
    friendsLayout->setContentsMargins(0, 0, 0, 0);

    m_friendsList = new QListWidget;
    m_friendsList->setStyleSheet(
        "QListWidget { border:1px solid #a0c0a0; border-radius:4px; }"
        "QListWidget::item { padding:8px; border-bottom:1px solid #eef4ee; }");
    friendsLayout->addWidget(m_friendsList, 1);

    auto* friendActionRow = new QHBoxLayout;
    m_challengeBtn = new QPushButton(QStringLiteral("🏆 发起挑战"));
    m_challengeBtn->setStyleSheet(
        "QPushButton { background:#ff9800; color:white; border:none;"
        " border-radius:6px; padding:6px 14px; font-weight:bold; }"
        "QPushButton:hover { background:#f57c00; }");
    m_challengeBtn->setEnabled(false);
    m_forestBtn = new QPushButton(QStringLiteral("🌲 好友森林"));
    m_forestBtn->setStyleSheet(
        "QPushButton { background:#7EC8A0; color:white; border:none;"
        " border-radius:6px; padding:6px 14px; font-weight:bold; }"
        "QPushButton:hover { background:#66bb8a; }");
    m_forestBtn->setEnabled(false);
    friendActionRow->addWidget(m_challengeBtn);
    friendActionRow->addWidget(m_forestBtn);
    friendActionRow->addStretch();
    friendsLayout->addLayout(friendActionRow);

    m_stack->addWidget(friendsPage);

    mainLayout->addWidget(m_stack, 1);

    // Close button
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setStyleSheet(
        "QPushButton { background:#999; color:white; border:none;"
        " border-radius:6px; padding:6px 20px; font-weight:bold; }"
        "QPushButton:hover { background:#777; }");
    mainLayout->addWidget(closeBtn, 0, Qt::AlignCenter);

    // Connections
    connect(m_browseBtn, &QPushButton::clicked, this, &FriendDialog::switchToBrowse);
    connect(m_reqsBtn, &QPushButton::clicked, this, &FriendDialog::switchToRequests);
    connect(m_friendsBtn, &QPushButton::clicked, this, &FriendDialog::switchToFriends);
    connect(searchBtn, &QPushButton::clicked, this, &FriendDialog::onSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &FriendDialog::onSearch);
    connect(sendBtn, &QPushButton::clicked, this, &FriendDialog::sendFriendRequest);
    connect(acceptBtn, &QPushButton::clicked, this, &FriendDialog::acceptRequest);
    connect(rejectBtn, &QPushButton::clicked, this, &FriendDialog::rejectRequest);
    connect(m_challengeBtn, &QPushButton::clicked, this, &FriendDialog::openChallenge);
    connect(m_forestBtn, &QPushButton::clicked, this, &FriendDialog::openFriendForest);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    switchToBrowse();
}

void FriendDialog::switchToBrowse()
{
    m_browseBtn->setChecked(true);
    m_reqsBtn->setChecked(false);
    m_friendsBtn->setChecked(false);
    m_stack->setCurrentIndex(0);
}

void FriendDialog::switchToRequests()
{
    m_browseBtn->setChecked(false);
    m_reqsBtn->setChecked(true);
    m_friendsBtn->setChecked(false);
    refreshRequestsPage();
    m_stack->setCurrentIndex(1);
}

void FriendDialog::switchToFriends()
{
    m_browseBtn->setChecked(false);
    m_reqsBtn->setChecked(false);
    m_friendsBtn->setChecked(true);
    refreshFriendsPage();
    m_stack->setCurrentIndex(2);
}

void FriendDialog::onSearch()
{
    refreshBrowsePage(m_searchEdit->text().trimmed());
}

void FriendDialog::refreshBrowsePage(const QString& filter)
{
    m_userList->clear();
    auto users = m_mgr->allUsers();

    for (const auto& u : users) {
        if (!filter.isEmpty() &&
            !u.username.contains(filter, Qt::CaseInsensitive))
            continue;

        QString status;
        if (m_mgr->isFriend(u.userId))
            status = QStringLiteral("✅ 好友");
        else {
            auto outgoing = m_mgr->outgoingRequests();
            bool hasPending = false;
            for (const auto& r : outgoing) {
                if (r.toUserId == u.userId) { hasPending = true; break; }
            }
            status = hasPending ? QStringLiteral("⏳ 已申请") : QStringLiteral("—");
        }

        auto* item = new QListWidgetItem(
            QStringLiteral("%1 (ID: %2)  %3").arg(u.username).arg(u.userId).arg(status));
        item->setData(Qt::UserRole, u.userId);
        m_userList->addItem(item);
    }
}

void FriendDialog::sendFriendRequest()
{
    auto* item = m_userList->currentItem();
    if (!item) {
        DialogPresenter::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个用户"));
        return;
    }
    uint32_t userId = item->data(Qt::UserRole).toUInt();
    if (m_mgr->sendRequest(userId)) {
        DialogPresenter::information(this, QStringLiteral("成功"), QStringLiteral("好友申请已发送！"));
        refreshBrowsePage(m_searchEdit->text().trimmed());
    } else {
        DialogPresenter::information(this, QStringLiteral("提示"), QStringLiteral("已发送过申请或不能添加自己"));
    }
}

void FriendDialog::refreshRequestsPage()
{
    m_reqsList->clear();
    auto incoming = m_mgr->incomingRequests();

    if (incoming.isEmpty()) {
        m_reqsList->addItem(QStringLiteral("(暂无好友申请)"));
        m_reqsList->item(0)->setFlags(Qt::NoItemFlags);
    }

    auto users = m_mgr->allUsers();
    for (const auto& r : incoming) {
        QString name = QStringLiteral("用户 #%1").arg(r.fromUserId);
        for (const auto& u : users) {
            if (u.userId == r.fromUserId) {
                name = u.username;
                break;
            }
        }
        auto* item = new QListWidgetItem(
            QStringLiteral("📩 %1 请求加你为好友").arg(name));
        item->setData(Qt::UserRole, r.fromUserId);
        m_reqsList->addItem(item);
    }

    m_reqsBadge->setText(incoming.isEmpty() ? QString() :
        QStringLiteral("(%1)").arg(incoming.size()));
}

void FriendDialog::acceptRequest()
{
    auto* item = m_reqsList->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) return;

    uint32_t fromUserId = item->data(Qt::UserRole).toUInt();
    if (m_mgr->acceptRequest(fromUserId)) {
        DialogPresenter::information(this, QStringLiteral("成功"), QStringLiteral("已同意好友申请！"));
        refreshRequestsPage();
        refreshFriendsPage();
    }
}

void FriendDialog::rejectRequest()
{
    auto* item = m_reqsList->currentItem();
    if (!item || !item->data(Qt::UserRole).isValid()) return;

    uint32_t fromUserId = item->data(Qt::UserRole).toUInt();
    if (m_mgr->rejectRequest(fromUserId)) {
        refreshRequestsPage();
    }
}

void FriendDialog::refreshFriendsPage()
{
    m_friendsList->clear();
    auto friends = m_mgr->friends();

    if (friends.isEmpty()) {
        m_friendsList->addItem(QStringLiteral("(暂无好友，快去查找用户添加吧)"));
        m_friendsList->item(0)->setFlags(Qt::NoItemFlags);
        m_challengeBtn->setEnabled(false);
        m_forestBtn->setEnabled(false);
    } else {
        m_challengeBtn->setEnabled(true);
        m_forestBtn->setEnabled(true);
    }

    for (const auto& f : friends) {
        m_friendsList->addItem(
            QStringLiteral("👤 %1 (ID: %2)").arg(f.username).arg(f.userId));
    }
}

void FriendDialog::openChallenge()
{
    auto* item = m_friendsList->currentItem();
    if (!item) {
        DialogPresenter::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在好友列表中选择一位好友"));
        return;
    }

    QVector<uint32_t> friendIds;
    auto friends = m_mgr->friends();
    for (const auto& f : friends)
        friendIds.append(f.userId);

    ChallengeDialog dlg(m_challengeMgr, friendIds, this);
    dlg.exec();
}

void FriendDialog::openFriendForest()
{
    auto* item = m_friendsList->currentItem();
    if (!item) {
        DialogPresenter::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先在好友列表中选择一位好友"));
        return;
    }
    uint32_t friendId = 0;
    auto friends = m_mgr->friends();
    auto* currentItem = m_friendsList->currentItem();
    for (const auto& f : friends) {
        if (QStringLiteral("👤 %1 (ID: %2)").arg(f.username).arg(f.userId)
            == currentItem->text()) {
            friendId = f.userId;
            break;
        }
    }
    if (friendId == 0) return;

    // Friend forest dialog: show friend's garden
    auto* friendDb = new DatabaseManager(
        QStringLiteral("data/user_%1/records.db").arg(friendId).toStdString());
    friendDb->open();

    auto records = friendDb->getAllRecords();
    QStringList info;
    uint32_t total = 0, success = 0, totalSecs = 0;
    for (const auto& r : records) {
        ++total;
        if (r.status == FocusRecordStatus::Success) { ++success; totalSecs += r.actualSeconds; }
    }

    QMessageBox msg(this);
    DialogPresenter::prepare(msg);
    msg.setWindowTitle(QStringLiteral("🌲 %1 的森林").arg(currentItem->text()));
    msg.setText(
        QStringLiteral("总专注次数: %1\n成功次数: %2\n成功率: %3%%\n总专注时长: %4 分钟")
            .arg(total).arg(success)
            .arg(total > 0 ? QString::number(success * 100.0 / total, 'f', 1) : "0")
            .arg(totalSecs / 60));
    msg.exec();

    delete friendDb;
}
