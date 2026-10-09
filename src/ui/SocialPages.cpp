#include "ui/SocialPages.h"

#include "core/FriendManager.h"
#include "ui/AppStyle.h"
#include "ui/DialogPresenter.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {

enum FriendRowRole {
    FriendNameRole = Qt::UserRole + 2,
    FriendDetailsRole,
    FriendStateRole,
    FriendStateToneRole
};

enum class FriendRowTone { Available, Pending, Connected };

void setFriendRow(QListWidgetItem& item, const QString& accessibleText, const QString& name,
                  const QString& details, const QString& state, FriendRowTone tone)
{
    item.setText(accessibleText);
    item.setToolTip(accessibleText);
    item.setData(FriendNameRole, name);
    item.setData(FriendDetailsRole, details);
    item.setData(FriendStateRole, state);
    item.setData(FriendStateToneRole, static_cast<int>(tone));
}

class FriendRowDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        const int height = qMax(40, QFontMetrics(option.font).height() + 20);
        return QSize(option.rect.width(), height);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem backgroundOption(option);
        initStyleOption(&backgroundOption, index);
        backgroundOption.text.clear();
        backgroundOption.features &= ~QStyleOptionViewItem::HasDisplay;
        QStyle* style = option.widget ? option.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &backgroundOption, painter, option.widget);

        const QRect row = option.rect.adjusted(12, 0, -12, 0);
        const QString name = index.data(FriendNameRole).toString();
        const QString details = index.data(FriendDetailsRole).toString();
        const QString state = index.data(FriendStateRole).toString();
        const QFontMetrics regularMetrics(option.font);
        QFont nameFont(option.font);
        nameFont.setWeight(QFont::DemiBold);
        const QFontMetrics nameMetrics(nameFont);
        const int stateColumnWidth = qMax(regularMetrics.horizontalAdvance(state) + 20, row.width() / 5);
        const int detailsColumnWidth = qMax(regularMetrics.horizontalAdvance(details) + 12, row.width() / 6);
        const int nameColumnWidth = qMax(0, row.width() - stateColumnWidth - detailsColumnWidth - 16);
        const QRect nameRect(row.left(), row.top(), nameColumnWidth, row.height());
        const QRect detailsRect(nameRect.right() + 8, row.top(), detailsColumnWidth, row.height());
        const QRect stateColumn(detailsRect.right() + 8, row.top(), stateColumnWidth, row.height());
        const int badgeWidth = qMin(stateColumn.width(), regularMetrics.horizontalAdvance(state) + 16);
        const QRect badge(stateColumn.right() - badgeWidth + 1,
                          stateColumn.center().y() - (regularMetrics.height() + 8) / 2,
                          badgeWidth, regularMetrics.height() + 8);

        const auto tone = static_cast<FriendRowTone>(index.data(FriendStateToneRole).toInt());
        const bool emphasized = tone == FriendRowTone::Available || tone == FriendRowTone::Connected;
        const QColor badgeColor = emphasized ? AppStyle::color(AppStyle::Color::Selection)
                                             : AppStyle::color(AppStyle::Color::Workspace);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeColor);
        painter->drawRoundedRect(badge, 4, 4);
        painter->setFont(nameFont);
        painter->setPen(AppStyle::color(AppStyle::Color::Text));
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                          nameMetrics.elidedText(name, Qt::ElideRight, nameRect.width()));
        painter->setFont(option.font);
        painter->setPen(AppStyle::color(AppStyle::Color::Muted));
        painter->drawText(detailsRect, Qt::AlignHCenter | Qt::AlignVCenter,
                          regularMetrics.elidedText(details, Qt::ElideRight, detailsRect.width()));
        painter->setPen(emphasized ? AppStyle::color(AppStyle::Color::Text)
                                   : AppStyle::color(AppStyle::Color::Muted));
        painter->drawText(badge, Qt::AlignCenter, state);
        painter->restore();
    }
};

QString pageStyle()
{
    const QString workspace = AppStyle::color(AppStyle::Color::Workspace).name();
    const QString surface = AppStyle::color(AppStyle::Color::Surface).name();
    const QString text = AppStyle::color(AppStyle::Color::Text).name();
    const QString muted = AppStyle::color(AppStyle::Color::Muted).name();
    const QString primary = AppStyle::color(AppStyle::Color::Primary).name();
    const QString primaryHover = AppStyle::color(AppStyle::Color::PrimaryHover).name();
    const QString primaryPressed = AppStyle::color(AppStyle::Color::PrimaryPressed).name();
    const QString selection = AppStyle::color(AppStyle::Color::Selection).name();
    const QString border = AppStyle::color(AppStyle::Color::Border).name();
    return QStringLiteral(
        "QWidget#friendsPage { background:%1; color:%3; }"
        "QWidget#friendsContent, QTabWidget, QTabWidget > QWidget > QWidget { background:transparent; }"
        "QWidget#friendsPage QStackedWidget#qt_tabwidget_stackedwidget { background:transparent; border:none; }"
        "QTabWidget::pane { border:none; background:transparent; top:0; }"
        "QTabBar::tab { background:transparent; color:%4; padding:8px 16px; border:none; border-bottom:2px solid transparent; font-size:14px; font-weight:600; min-width:88px; }"
        "QTabBar::tab:selected { color:%3; border-bottom:2px solid %5; }"
        "QTabBar::tab:hover { color:%3; }"
        "QTabBar { background:transparent; }"
        "QWidget#friendsPage QLabel { color:%3; background:transparent; border:none; font-size:14px; }"
        "QWidget#friendsPage QLabel[title=\"true\"] { font-size:24px; font-weight:700; }"
        "QWidget#friendsPage QLabel[summary=\"true\"] { color:%4; font-size:13px; }"
        "QWidget#friendsPage QLabel[localScope=\"true\"] { color:%5; background:%8; border-radius:4px; padding:4px 8px; font-size:13px; font-weight:600; }"
        "QWidget#friendsPage QLabel[emptyState=\"true\"] { color:%4; background:transparent; font-size:14px; }"
        "QWidget#friendsPage QLineEdit { background:%2; color:%3; border:1px solid %9; border-radius:6px; padding:7px 10px; font-size:14px; selection-background-color:%8; selection-color:%3; }"
        "QWidget#friendsPage QLineEdit:focus { border:2px solid %5; padding:6px 9px; }"
        "QWidget#friendsPage QListWidget { background:%2; color:%3; border:1px solid %9; border-radius:6px; padding:4px; font-size:14px; }"
        "QListWidget::item { min-height:38px; border-radius:4px; }"
        "QListWidget::item:hover { background:%1; }"
        "QListWidget::item:selected { background:%8; color:%3; }"
        "QListWidget:focus { border-color:%5; }"
        "QWidget#friendsPage QPushButton#btnPrimary { background:%5; color:%2; border:1px solid %5; border-radius:6px; padding:0 14px; min-height:36px; font-size:14px; }"
        "QWidget#friendsPage QPushButton#btnPrimary:hover { background:%6; border-color:%6; }"
        "QWidget#friendsPage QPushButton#btnPrimary:pressed { background:%7; border-color:%7; }"
        "QWidget#friendsPage QPushButton#btnSecondary { background:%2; color:%3; border:1px solid %9; border-radius:6px; padding:0 14px; min-height:36px; font-size:14px; }"
        "QWidget#friendsPage QPushButton#btnSecondary:hover { background:%8; border-color:%5; }"
        "QWidget#friendsPage QPushButton#btnPrimary:disabled, QWidget#friendsPage QPushButton#btnSecondary:disabled { background:%1; border-color:%9; color:%4; }"
        "QWidget#friendsPage QPushButton:focus { border:2px solid %5; }")
        .arg(workspace).arg(surface).arg(text).arg(muted).arg(primary).arg(primaryHover)
        .arg(primaryPressed).arg(selection).arg(border);
}

QPushButton* pageButton(const QString& text, QWidget* parent, bool primary = false)
{
    auto* button = new QPushButton(text, parent);
    if (primary) DialogPresenter::setPrimary(button);
    else DialogPresenter::setSecondary(button);
    button->setMinimumHeight(40);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QLabel* emptyState(QWidget* parent, const char* name, const QString& text)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(name);
    label->setProperty("emptyState", "true");
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

} // namespace

FriendsPageController::FriendsPageController(FriendManager& friends, QWidget* parent)
    : QObject(parent), friends_(friends)
{
    page_ = new QWidget(parent);
    page_->setObjectName("friendsPage");
    page_->setStyleSheet(pageStyle());
    AppStyle::applyAccessibility(*page_);
    auto* outer = new QHBoxLayout(page_);
    outer->setContentsMargins(24, 18, 24, 18);
    auto* content = new QWidget(page_);
    content->setObjectName("friendsContent");
    content->setMaximumWidth(1120);
    outer->addStretch(); outer->addWidget(content, 1); outer->addStretch();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("用户与好友"), content);
    title->setProperty("title", "true");
    titleRow->addWidget(title);
    summaryLabel_ = new QLabel(content);
    summaryLabel_->setProperty("summary", "true");
    titleRow->addSpacing(12);
    titleRow->addWidget(summaryLabel_);
    titleRow->addStretch();
    auto* localMode = new QLabel(QStringLiteral("本机 · 不联网"), content);
    localMode->setProperty("localScope", "true");
    localMode->setToolTip(QStringLiteral("好友与申请仅保存在本机，不进行网络同步。"));
    localMode->setAccessibleDescription(localMode->toolTip());
    titleRow->addWidget(localMode);
    layout->addLayout(titleRow);
    auto* tabs = new QTabWidget(content);
    tabs->setObjectName("friendTabs");

    auto* browse = new QWidget(tabs); auto* browseLayout = new QVBoxLayout(browse);
    auto* searchRow = new QHBoxLayout; searchInput_ = new QLineEdit(browse); searchInput_->setObjectName("friendSearchInput"); searchInput_->setPlaceholderText(QStringLiteral("搜索用户名或编号"));
    browseLayout->setContentsMargins(12, 10, 12, 12); browseLayout->setSpacing(12);
    searchInput_->setMinimumHeight(40);
    auto* search = pageButton(QStringLiteral("搜索"), browse); auto* send = pageButton(QStringLiteral("发送好友申请"), browse, true);
    sendButton_ = send;
    search->setProperty("testId", QStringLiteral("friendSearchButton"));
    send->setProperty("testId", QStringLiteral("friendSendRequestButton"));
    search->setToolTip(QStringLiteral("按用户名或编号搜索本机账户。"));
    send->setToolTip(QStringLiteral("向当前选中的用户发送好友申请。"));
    searchRow->addWidget(searchInput_, 1); searchRow->addWidget(search); searchRow->addWidget(send); browseLayout->addLayout(searchRow);
    userList_ = new QListWidget(browse); userList_->setObjectName("friendUserList");
    userList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    userList_->setTextElideMode(Qt::ElideRight);
    userList_->setItemDelegate(new FriendRowDelegate(userList_));
    browseEmpty_ = emptyState(browse, "friendBrowseEmpty", QStringLiteral("没有找到匹配的本机账户"));
    browseLayout->addWidget(browseEmpty_, 1); browseLayout->addWidget(userList_, 1);
    tabs->addTab(browse, QStringLiteral("查找用户"));

    auto* requests = new QWidget(tabs); auto* requestsLayout = new QVBoxLayout(requests);
    requestsLayout->setContentsMargins(12, 10, 12, 12); requestsLayout->setSpacing(12);
    requestList_ = new QListWidget(requests); requestList_->setObjectName("friendRequestList");
    requestList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    requestList_->setTextElideMode(Qt::ElideRight);
    requestList_->setItemDelegate(new FriendRowDelegate(requestList_));
    requestsEmpty_ = emptyState(requests, "friendRequestsEmpty", QStringLiteral("暂无待处理的好友申请"));
    requestsLayout->addWidget(requestsEmpty_, 1); requestsLayout->addWidget(requestList_, 1);
    auto* requestActions = new QHBoxLayout; auto* accept = pageButton(QStringLiteral("同意"), requests, true); auto* reject = pageButton(QStringLiteral("拒绝"), requests);
    acceptButton_ = accept; rejectButton_ = reject;
    accept->setProperty("testId", QStringLiteral("friendAcceptButton"));
    reject->setProperty("testId", QStringLiteral("friendRejectButton"));
    requestActions->addStretch(); requestActions->addWidget(accept); requestActions->addWidget(reject); requestsLayout->addLayout(requestActions); tabs->addTab(requests, QStringLiteral("好友申请"));

    auto* mine = new QWidget(tabs); auto* mineLayout = new QVBoxLayout(mine);
    mineLayout->setContentsMargins(12, 10, 12, 12); mineLayout->setSpacing(12);
    friendList_ = new QListWidget(mine); friendList_->setObjectName("friendList");
    friendList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    friendList_->setTextElideMode(Qt::ElideRight);
    friendList_->setItemDelegate(new FriendRowDelegate(friendList_));
    friendsEmpty_ = emptyState(mine, "friendListEmpty", QStringLiteral("暂无好友"));
    mineLayout->addWidget(friendsEmpty_, 1); mineLayout->addWidget(friendList_, 1);
    auto* friendActions = new QHBoxLayout; auto* challenge = pageButton(QStringLiteral("发起挑战"), mine, true); auto* forest = pageButton(QStringLiteral("好友森林概览"), mine);
    challenge->setProperty("testId", QStringLiteral("friendChallengeButton"));
    forest->setEnabled(false);
    forest->setToolTip(QStringLiteral("好友森林详情暂未实现。"));
    forest->setCursor(Qt::ArrowCursor);
    challengeButton_ = challenge; friendActions->addWidget(challenge); friendActions->addWidget(forest); friendActions->addStretch(); mineLayout->addLayout(friendActions); tabs->addTab(mine, QStringLiteral("我的好友"));
    layout->addWidget(tabs, 1); layout->addStretch();
    connect(userList_, &QListWidget::currentItemChanged, this, [this] { refreshActions(); });
    connect(requestList_, &QListWidget::currentItemChanged, this, [this] { refreshActions(); });
    connect(friendList_, &QListWidget::currentItemChanged, this, [this] { refreshActions(); });

    connect(search, &QPushButton::clicked, this, [this] { refreshBrowse(searchInput_->text().trimmed()); });
    connect(searchInput_, &QLineEdit::returnPressed, this, [this] { refreshBrowse(searchInput_->text().trimmed()); });
    connect(send, &QPushButton::clicked, this, [this] {
        auto* item = userList_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) { DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一个用户。")); return; }
        const uint32_t targetId = item->data(Qt::UserRole).toUInt();
        if (friends_.sendRequest(targetId)) {
            DialogPresenter::information(page_, QStringLiteral("已发送"), QStringLiteral("好友申请已发送。"));
        } else {
            const QString message = friends_.isFriend(targetId)
                ? QStringLiteral("该用户已是好友，不能重复申请。")
                : QStringLiteral("好友申请未能发送，可能已有待处理申请或保存失败。");
            DialogPresenter::information(page_, QStringLiteral("提示"), message);
        }
        refreshBrowse(searchInput_->text().trimmed());
    });
    connect(accept, &QPushButton::clicked, this, [this] {
        auto* item = requestList_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) {
            DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一条好友申请。"));
            return;
        }
        if (!friends_.acceptRequest(item->data(Qt::UserRole).toUInt()))
            DialogPresenter::information(page_, QStringLiteral("提示"),
                                         QStringLiteral("好友关系未更新，申请状态可能已变化或保存失败。"));
        refresh();
    });
    connect(reject, &QPushButton::clicked, this, [this] {
        auto* item = requestList_->currentItem();
        if (!item || !item->data(Qt::UserRole).isValid()) {
            DialogPresenter::information(page_, QStringLiteral("提示"), QStringLiteral("请先选择一条好友申请。"));
            return;
        }
        if (!friends_.rejectRequest(item->data(Qt::UserRole).toUInt()))
            DialogPresenter::information(page_, QStringLiteral("提示"),
                                         QStringLiteral("好友申请未能拒绝，状态可能已变化或保存失败。"));
        refresh();
    });
    connect(challenge, &QPushButton::clicked, this, [this] {
        if (auto* item = friendList_->currentItem(); item && item->data(Qt::UserRole).isValid())
            emit challengeRequested();
    });
    refresh();
}

void FriendsPageController::refresh(const QString& filter)
{
    refreshBrowse(filter.isNull() ? searchInput_->text().trimmed() : filter);
    refreshRequests();
    refreshFriends();
}
void FriendsPageController::refreshBrowse(const QString& filter)
{
    const auto* selected = userList_->currentItem();
    const uint32_t selectedId = selected ? selected->data(Qt::UserRole).toUInt() : 0;
    userList_->clear(); const auto outgoing = friends_.outgoingRequests();
    const auto incoming = friends_.incomingRequests();
    for (const auto& user : friends_.allUsers()) {
        if (!filter.isEmpty() && !user.username.contains(filter, Qt::CaseInsensitive) && !QString::number(user.userId).contains(filter)) continue;
        bool pending = false; for (const auto& request : outgoing) if (request.toUserId == user.userId) { pending = true; break; }
        bool awaiting = false;
        for (const auto& request : incoming) if (request.fromUserId == user.userId) { awaiting = true; break; }
        const bool isFriend = friends_.isFriend(user.userId);
        const QString state = isFriend ? QStringLiteral("已是好友") : (pending ? QStringLiteral("已申请")
            : (awaiting ? QStringLiteral("待处理申请") : QStringLiteral("可申请")));
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, user.userId);
        item->setData(Qt::UserRole + 1, isFriend || pending || awaiting);
        const FriendRowTone tone = isFriend ? FriendRowTone::Connected
            : (pending || awaiting ? FriendRowTone::Pending : FriendRowTone::Available);
        setFriendRow(*item, QStringLiteral("%1    编号 %2    ·    %3").arg(user.username).arg(user.userId).arg(state),
                     user.username, QStringLiteral("编号 %1").arg(user.userId), state, tone);
        userList_->addItem(item);
        if (user.userId == selectedId) userList_->setCurrentItem(item);
    }
    browseEmpty_->setVisible(userList_->count() == 0);
    userList_->setVisible(userList_->count() != 0);
    refreshActions();
}
void FriendsPageController::refreshRequests()
{
    requestList_->clear();
    const auto users = friends_.allUsers();
    for (const auto& request : friends_.incomingRequests()) {
        QString name = QStringLiteral("用户 %1").arg(request.fromUserId);
        for (const auto& user : users) {
            if (user.userId == request.fromUserId) {
                name = user.username;
                break;
            }
        }
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, request.fromUserId);
        setFriendRow(*item, QStringLiteral("%1 请求添加你为好友").arg(name), name,
                     QStringLiteral("编号 %1").arg(request.fromUserId), QStringLiteral("待处理"),
                     FriendRowTone::Pending);
        requestList_->addItem(item);
    }
    requestsEmpty_->setVisible(requestList_->count() == 0);
    requestList_->setVisible(requestList_->count() != 0);
    refreshActions();
}
void FriendsPageController::refreshFriends()
{
    friendList_->clear();
    const auto friends = friends_.friends();
    for (const auto& friendInfo : friends) {
        auto* item = new QListWidgetItem;
        item->setData(Qt::UserRole, friendInfo.userId);
        setFriendRow(*item, QStringLiteral("%1    编号 %2").arg(friendInfo.username).arg(friendInfo.userId),
                     friendInfo.username, QStringLiteral("编号 %1").arg(friendInfo.userId),
                     QStringLiteral("好友"), FriendRowTone::Connected);
        friendList_->addItem(item);
    }
    friendsEmpty_->setVisible(friends.isEmpty());
    friendList_->setVisible(!friends.isEmpty());
    summaryLabel_->setText(QStringLiteral("好友 %1 人，待处理申请 %2 个").arg(friends.size()).arg(friends_.pendingCount()));
    refreshActions();
}

void FriendsPageController::refreshActions()
{
    const auto* user = userList_->currentItem();
    const bool canSend = user && user->data(Qt::UserRole).isValid() && !user->data(Qt::UserRole + 1).toBool();
    sendButton_->setEnabled(canSend);
    sendButton_->setToolTip(!user ? QStringLiteral("请先选择一个本机账户。")
        : canSend ? QStringLiteral("向所选账户发送好友申请。") : QStringLiteral("该账户已是好友或有待处理申请。"));
    const auto* request = requestList_->currentItem();
    const bool canRespond = request && request->data(Qt::UserRole).isValid();
    acceptButton_->setEnabled(canRespond); rejectButton_->setEnabled(canRespond);
    acceptButton_->setToolTip(canRespond ? QStringLiteral("同意所选好友申请。") : QStringLiteral("请先选择一条好友申请。"));
    rejectButton_->setToolTip(canRespond ? QStringLiteral("拒绝所选好友申请。") : QStringLiteral("请先选择一条好友申请。"));
    const auto* selectedFriend = friendList_->currentItem();
    const bool canChallenge = selectedFriend && selectedFriend->data(Qt::UserRole).isValid();
    challengeButton_->setEnabled(canChallenge);
    challengeButton_->setToolTip(canChallenge ? QStringLiteral("打开本地专注挑战。") : QStringLiteral("请先选择一位好友。"));
}
