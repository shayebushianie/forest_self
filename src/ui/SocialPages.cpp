#include "ui/SocialPages.h"

#include "core/FriendManager.h"
#include "ui/DialogPresenter.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

QString pageStyle(const QString& selector)
{
    return QStringLiteral(
        "QWidget#%1 { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #58AD8F, stop:0.62 #4FA286, stop:1 #4A907B); }"
        "QFrame[panel=\"true\"] { background:rgba(255,255,255,32); border:1px solid rgba(255,255,255,70); border-radius:8px; }"
        "QLabel[title=\"true\"] { color:#F7FFF7; font-size:20px; font-weight:900; background:transparent; border:none; }"
        "QLabel[subtle=\"true\"] { color:rgba(247,255,247,178); font-size:13px; background:transparent; border:none; }"
        "QLineEdit { background:rgba(255,255,255,224); color:#2B6C54; border:none; border-radius:7px; padding:8px 10px; }"
        "QListWidget { background:rgba(255,255,255,24); color:#F7FFF7; border:1px solid rgba(255,255,255,50); border-radius:8px; padding:4px; }"
        "QListWidget::item { padding:9px; border-radius:6px; }"
        "QListWidget::item:selected { background:rgba(242,244,198,72); }"
        "QPushButton[action=\"true\"] { background:#F2F4C6; color:#3E8D75; border:none; border-radius:8px; padding:8px 16px; font-weight:900; }"
        "QPushButton[action=\"true\"]:hover { background:#FFFFFF; }"
        "QPushButton[danger=\"true\"] { background:#D9534F; color:white; border:none; border-radius:8px; padding:8px 16px; font-weight:900; }").arg(selector);
}

QPushButton* pageButton(const QString& text, QWidget* parent, bool danger = false)
{
    auto* button = new QPushButton(text, parent);
    button->setObjectName(danger ? QStringLiteral("btnDanger") : QStringLiteral("btnPrimary"));
    button->setProperty(danger ? "danger" : "action", "true");
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void addEmptyState(QListWidget* list, const QString& text)
{
    auto* item = new QListWidgetItem(QStringLiteral("— %1 —").arg(text));
    item->setFlags(Qt::NoItemFlags);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(QColor(247, 255, 247, 165));
    list->addItem(item);
}

} // namespace

FriendsPageController::FriendsPageController(FriendManager& friends, QWidget* parent)
    : QObject(parent), friends_(friends)
{
    page_ = new QWidget(parent);
    page_->setObjectName("friendsPage");
    page_->setStyleSheet(pageStyle(QStringLiteral("friendsPage")) +
        "QTabWidget::pane { border:1px solid rgba(255,255,255,70); border-radius:8px; background:rgba(255,255,255,28); top:-1px; }"
        "QTabBar::tab { background:rgba(255,255,255,22); color:rgba(247,255,247,190); padding:9px 18px; border-top-left-radius:8px; border-top-right-radius:8px; font-weight:800; min-width:96px; }"
        "QTabBar::tab:selected { background:rgba(242,244,198,58); color:#F7FFF7; }");
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 22, 26, 22); layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("👥 用户与好友"), page_);
    title->setProperty("title", "true"); layout->addWidget(title);
    summaryLabel_ = new QLabel(page_); summaryLabel_->setProperty("subtle", "true"); layout->addWidget(summaryLabel_);
    auto* tabs = new QTabWidget(page_);

    auto* browse = new QWidget(tabs); auto* browseLayout = new QVBoxLayout(browse);
    auto* searchRow = new QHBoxLayout; searchInput_ = new QLineEdit(browse); searchInput_->setObjectName("friendSearchInput"); searchInput_->setPlaceholderText(QStringLiteral("搜索用户名或 ID"));
    auto* search = pageButton(QStringLiteral("搜索"), browse); auto* send = pageButton(QStringLiteral("发送好友申请"), browse);
    search->setProperty("testId", QStringLiteral("friendSearchButton"));
    send->setProperty("testId", QStringLiteral("friendSendRequestButton"));
    search->setToolTip(QStringLiteral("按用户名或 ID 搜索本地账户。"));
    send->setToolTip(QStringLiteral("向当前选中的用户发送好友申请。"));
    searchRow->addWidget(searchInput_, 1); searchRow->addWidget(search); searchRow->addWidget(send); browseLayout->addLayout(searchRow);
    userList_ = new QListWidget(browse); userList_->setObjectName("friendUserList"); browseLayout->addWidget(userList_, 1); tabs->addTab(browse, QStringLiteral("查找用户"));

    auto* requests = new QWidget(tabs); auto* requestsLayout = new QVBoxLayout(requests); requestList_ = new QListWidget(requests); requestsLayout->addWidget(requestList_, 1);
    auto* requestActions = new QHBoxLayout; auto* accept = pageButton(QStringLiteral("同意"), requests); auto* reject = pageButton(QStringLiteral("拒绝"), requests, true);
    accept->setProperty("testId", QStringLiteral("friendAcceptButton"));
    reject->setProperty("testId", QStringLiteral("friendRejectButton"));
    requestActions->addStretch(); requestActions->addWidget(accept); requestActions->addWidget(reject); requestsLayout->addLayout(requestActions); tabs->addTab(requests, QStringLiteral("好友申请"));

    auto* mine = new QWidget(tabs); auto* mineLayout = new QVBoxLayout(mine); friendList_ = new QListWidget(mine); mineLayout->addWidget(friendList_, 1);
    auto* friendActions = new QHBoxLayout; auto* challenge = pageButton(QStringLiteral("发起挑战"), mine); auto* forest = pageButton(QStringLiteral("好友森林概览"), mine);
    challenge->setProperty("testId", QStringLiteral("friendChallengeButton"));
    forest->setEnabled(false);
    forest->setToolTip(QStringLiteral("本地模式暂不支持查看其他设备的森林数据。"));
    challengeButton_ = challenge; friendActions->addWidget(challenge); friendActions->addWidget(forest); friendActions->addStretch(); mineLayout->addLayout(friendActions); tabs->addTab(mine, QStringLiteral("我的好友"));
    layout->addWidget(tabs, 1);

    connect(search, &QPushButton::clicked, this, [this] { refreshBrowse(searchInput_->text().trimmed()); });
    connect(searchInput_, &QLineEdit::returnPressed, this, [this] { refreshBrowse(searchInput_->text().trimmed()); });
    connect(send, &QPushButton::clicked, this, [this] {
        auto* item = userList_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) { DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一个用户。")); return; }
        DialogPresenter::information(page_, friends_.sendRequest(item->data(Qt::UserRole).toUInt()) ? QStringLiteral("已发送") : QStringLiteral("提示"),
                                 friends_.isFriend(item->data(Qt::UserRole).toUInt()) ? QStringLiteral("已是好友，或不能添加自己。") : QStringLiteral("好友申请已发送。"));
        refreshBrowse(searchInput_->text().trimmed());
    });
    connect(accept, &QPushButton::clicked, this, [this] { auto* item = requestList_->currentItem(); if (!item || !item->data(Qt::UserRole).isValid()) { DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一条好友申请。")); return; } friends_.acceptRequest(item->data(Qt::UserRole).toUInt()); refresh(); });
    connect(reject, &QPushButton::clicked, this, [this] { auto* item = requestList_->currentItem(); if (!item || !item->data(Qt::UserRole).isValid()) { DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一条好友申请。")); return; } friends_.rejectRequest(item->data(Qt::UserRole).toUInt()); refresh(); });
    connect(challenge, &QPushButton::clicked, this, [this] { if (!friendList_->currentItem()) { DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一位好友。")); return; } emit challengeRequested(); });
    refresh();
}

void FriendsPageController::refresh(const QString& filter) { refreshBrowse(filter); refreshRequests(); refreshFriends(); }
void FriendsPageController::refreshBrowse(const QString& filter)
{
    userList_->clear(); const auto outgoing = friends_.outgoingRequests();
    for (const auto& user : friends_.allUsers()) {
        if (!filter.isEmpty() && !user.username.contains(filter, Qt::CaseInsensitive) && !QString::number(user.userId).contains(filter)) continue;
        bool pending = false; for (const auto& request : outgoing) if (request.toUserId == user.userId) { pending = true; break; }
        const QString state = friends_.isFriend(user.userId) ? QStringLiteral("已是好友") : (pending ? QStringLiteral("已申请") : QStringLiteral("可申请"));
        auto* item = new QListWidgetItem(QStringLiteral("%1    ID: %2    %3").arg(user.username).arg(user.userId).arg(state)); item->setData(Qt::UserRole, user.userId); userList_->addItem(item);
    }
    if (!userList_->count()) addEmptyState(userList_, QStringLiteral("没有找到匹配用户"));
}
void FriendsPageController::refreshRequests()
{
    requestList_->clear(); const auto users = friends_.allUsers();
    for (const auto& request : friends_.incomingRequests()) { QString name = QStringLiteral("用户 %1").arg(request.fromUserId); for (const auto& user : users) if (user.userId == request.fromUserId) { name = user.username; break; } auto* item = new QListWidgetItem(QStringLiteral("%1 请求添加你为好友").arg(name)); item->setData(Qt::UserRole, request.fromUserId); requestList_->addItem(item); }
    if (!requestList_->count()) addEmptyState(requestList_, QStringLiteral("暂无好友申请"));
}
void FriendsPageController::refreshFriends()
{
    friendList_->clear(); const auto friends = friends_.friends();
    for (const auto& friendInfo : friends) { auto* item = new QListWidgetItem(QStringLiteral("%1    ID: %2").arg(friendInfo.username).arg(friendInfo.userId)); item->setData(Qt::UserRole, friendInfo.userId); friendList_->addItem(item); }
    if (friends.isEmpty()) addEmptyState(friendList_, QStringLiteral("暂无好友，可先搜索并发送申请"));
    summaryLabel_->setText(QStringLiteral("好友 %1 人，待处理申请 %2 个").arg(friends.size()).arg(friends_.pendingCount()));
    challengeButton_->setEnabled(!friends.isEmpty());
}
