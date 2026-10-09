#include "ui/AppStyle.h"
#include "ui/ChallengeDashboardWidget.h"

#include "config/PlantCatalog.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"
#include "ui/PaintedActionButton.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QTime>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

namespace {
using RewardState = ChallengeRewardService::RewardState;

QFont challengeFont(int pixels, int weight = QFont::Normal)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(pixels);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return AppStyle::scaledFont(font);
}

struct ChallengeLayout {
    qreal title = QFontMetrics(challengeFont(24, QFont::Bold)).height();
    qreal section = QFontMetrics(challengeFont(18, QFont::Bold)).height();
    qreal body = QFontMetrics(challengeFont(14)).height();
    qreal small = QFontMetrics(challengeFont(12)).height();
    qreal icon = QFontMetrics(challengeFont(28)).height();
    qreal headerHeight() const { return title + small + 8; }
    qreal monthHeight() const { return 96 + title + body * 3; }
    qreal sectionOffset() const { return section + 14; }
    qreal checkinHeight() const { return sectionOffset() + icon + body + small + 24; }
    qreal taskHeight() const { return 68 + body * 2 + small * 2; }
};

void challengeFeedback(QWidget* parent, const QString& title, const QString& message,
                       QMessageBox::Icon icon = QMessageBox::Information)
{
    if (icon == QMessageBox::Warning) DialogPresenter::warning(parent, title, message);
    else if (icon == QMessageBox::Critical) DialogPresenter::error(parent, title, message);
    else DialogPresenter::information(parent, title, message);
}

QString checkinStateText(RewardState state)
{
    switch (state) {
    case RewardState::Available: return QStringLiteral("今日可领取");
    case RewardState::Claimed: return QStringLiteral("已领取");
    case RewardState::Locked: return QStringLiteral("未解锁");
    case RewardState::Expired: return QStringLiteral("过期");
    case RewardState::NotReady: return QStringLiteral("未完成");
    case RewardState::Unavailable: return QStringLiteral("暂不可领取");
    }
    return {};
}
}

ChallengeDashboardWidget::ChallengeDashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("challengeDashboard");
    setMinimumSize(720, 720);
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

void ChallengeDashboardWidget::setSnapshot(uint32_t coins,
                                           const QVector<Challenge>& challenges,
                                           const std::vector<FocusRecord>& records)
{
    coins_ = coins;
    challenges_ = challenges;
    records_ = records;
    update();
}

void ChallengeDashboardWidget::setRewardService(ChallengeRewardService* service)
{
    rewardService_ = service;
    update();
}

QVector<ChallengeDashboardWidget::PlantInfo> ChallengeDashboardWidget::shopPlants() const
{
    QVector<PlantInfo> plants;
    plants.reserve(PlantCatalog::all().size());
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        PlantInfo info;
        info.name = plant.displayName;
        info.rarity = QStringLiteral("商城植物");
        info.description = QStringLiteral("可在植物商城解锁，并用于专注与挑战奖励展示。");
        info.condition = plant.type == PlantTypeId::Oak ? QStringLiteral("默认可用")
                                         : QStringLiteral("在植物商城使用金币解锁");
        info.source = QStringLiteral("植物商城");
        info.iconPath = plant.iconPath;
        info.type = plant.type;
        plants.push_back(info);
    }
    return plants;
}

ChallengeDashboardWidget::DataModel ChallengeDashboardWidget::buildData(const QDate& date) const
{
    DataModel data;
    const auto plants = shopPlants();
    const auto progress = rewardService_ ? rewardService_->snapshot(date, challenges_, records_)
                                        : ChallengeRewardService::progress(date, challenges_, records_);
    data.date = date;
    data.monthlyState = rewardService_ ? progress.monthlyState : RewardState::Unavailable;
    data.rewardError = rewardService_ ? progress.error : QStringLiteral("尚未绑定当前账户的奖励服务。");
    data.todayMinutes = progress.todayMinutes;
    data.todaySessions = progress.todaySessions;
    data.monthMinutes = progress.monthMinutes;
    data.monthTarget = progress.monthTarget;
    data.remainingDays = progress.remainingDays;
    data.rewardGrowth = progress.growth;
    data.monthlyCoins = progress.monthlyCoins;
    for (const auto& plant : plants) if (plant.type == progress.plantType) data.rewardPlant = plant;

    for (const auto& checkin : progress.checkins) {
        CheckinReward reward;
        reward.icon = QStringLiteral("🪙");
        reward.coinReward = checkin.coins;
        reward.text = QStringLiteral("x%1").arg(reward.coinReward);

        reward.state = rewardService_ ? checkin.state : RewardState::Unavailable;
        data.checkins.append(reward);
    }

    const QColor chestColors[] = {QColor("#FFC93D"), QColor("#6AA9E9"), QColor("#B874F0"), QColor("#F4A84A")};
    for (int i = 0; i < progress.tasks.size(); ++i) {
        const auto& task = progress.tasks[i];
        data.tasks.append({task.title, task.category, task.description, QStringLiteral("🪙"), task.actionText,
                           task.progress, task.target, task.coins, chestColors[i % std::size(chestColors)],
                           task.bonus, true, task.type, task.plantType,
                           rewardService_ ? task.state : RewardState::Unavailable});
    }

    return data;
}

void ChallengeDashboardWidget::paintEvent(QPaintEvent*)
{
    hitRegions_.clear();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    painter.fillRect(rect(), AppStyle::color(AppStyle::Color::Workspace));

    const DataModel data = buildData(QDate::currentDate());
    displayedDate_ = data.date;
    const qreal contentWidth = std::min<qreal>(1160, width() - 60);
    const QRectF content((width() - contentWidth) / 2, 24, contentWidth, height() - 52);

    const ChallengeLayout metrics;
    QRectF header(content.left(), content.top(), content.width(), metrics.headerHeight());
    drawHeader(painter, header, data);
    QRectF monthly(content.left(), header.bottom() + 20, content.width(), metrics.monthHeight());
    drawMonthlyCard(painter, monthly, data);
    const QRectF checkin(content.left(), monthly.bottom() + 24, content.width(), metrics.checkinHeight());
    const int columns = content.width() < 900 ? 1 : 2;
    const int rows = std::max(1, (static_cast<int>(data.tasks.size()) + columns - 1) / columns);
    const QRectF tasks(content.left(), checkin.bottom() + 24, content.width(),
                       metrics.sectionOffset() + rows * metrics.taskHeight() + (rows - 1) * 16);
    drawCheckinCard(painter, checkin, data);
    drawTaskPanel(painter, tasks, data);
    if (minimumHeight() != qCeil(tasks.bottom() + 28)) setMinimumHeight(qCeil(tasks.bottom() + 28));
    syncKeyboardActions();
}

void ChallengeDashboardWidget::mouseMoveEvent(QMouseEvent* event)
{
    rebuildCursor(event->pos());
}

void ChallengeDashboardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    const int columns = std::min(width() - 60, 1160) < 900 ? 1 : 2;
    const int count = buildData(QDate::currentDate()).tasks.size();
    const int rows = std::max(1, (count + columns - 1) / columns);
    const ChallengeLayout metrics;
    setMinimumHeight(qCeil(24 + metrics.headerHeight() + 20 + metrics.monthHeight() + 24 +
        metrics.checkinHeight() + 24 + metrics.sectionOffset() +
        rows * metrics.taskHeight() + (rows - 1) * 16 + 28));
}

void ChallengeDashboardWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const HitRegion* region = hitAt(event->pos());
    if (!region) return;
    activateHit(*region);
}

void ChallengeDashboardWidget::activateHit(const HitRegion& region)
{
    const HitRegion hit = region;
    if (displayedDate_ != QDate::currentDate()) {
        hitRegions_.clear();
        update();
        challengeFeedback(this, QStringLiteral("挑战已刷新"),
                                     QStringLiteral("奖励周期已刷新，请查看新的挑战后重试。"));
        return;
    }

    const DataModel data = buildData(displayedDate_);
    switch (hit.role) {
    case HitRole::Coin:
    case HitRole::Growth:
        showResourceDialog(hit.role, data);
        break;
    case HitRole::MonthlyCard:
    case HitRole::MonthlyProgress:
        showMonthlyDialog(data);
        break;
    case HitRole::MonthlyTime:
        showRefreshDialog(true);
        break;
    case HitRole::MonthlyReward:
        claimMonthlyReward(data);
        break;
    case HitRole::MonthlyPlant:
        showPlantDialog(data.rewardPlant);
        break;
    case HitRole::CheckinRefresh:
        showRefreshDialog(false);
        break;
    case HitRole::CheckinReward:
        claimCheckinReward(data, hit.index);
        break;
    case HitRole::TaskCard:
    case HitRole::TaskProgress:
        showTaskDialog(data, hit.index);
        break;
    case HitRole::TaskChest:
        claimTaskReward(data, hit.index);
        break;
    case HitRole::TaskPlus:
        if (hit.index >= 0 && hit.index < data.tasks.size()) showPlusDialog(data.tasks[hit.index]);
        break;
    case HitRole::TaskArrow:
        if (hit.index >= 0 && hit.index < data.tasks.size()) routeTaskAction(data.tasks[hit.index]);
        break;
    default:
        break;
    }
}

void ChallengeDashboardWidget::syncKeyboardActions()
{
    int index = 0;
    for (const HitRegion& region : hitRegions_) {
        // The card already opens the same detail; avoid duplicate progress focus stops.
        if (region.role == HitRole::MonthlyProgress || region.role == HitRole::TaskProgress) continue;
        if (index == keyboardActions_.size()) {
            auto* button = new PaintedActionButton(this);
            keyboardActions_.append(button);
        }
        auto* button = keyboardActions_[index];
        button->setObjectName(QStringLiteral("challengeKeyboardAction%1").arg(index));
        static const char* actionNames[] = { "none", "coin", "growth", "monthly", "monthlyProgress",
            "monthlyRefresh", "monthlyReward", "plant", "checkinRefresh", "checkinReward",
            "task", "taskProgress", "taskReward", "taskBonus", "taskAction" };
        button->setProperty("challengeAction", actionNames[static_cast<int>(region.role)]);
        button->setProperty("challengeIndex", region.index);
        const QString name = region.tooltip.isEmpty() ? QStringLiteral("查看挑战详情") : region.tooltip;
        button->bind(region.rect, name, [this, region] { activateHit(region); });
        if (index > 0) QWidget::setTabOrder(keyboardActions_[index - 1], button);
        ++index;
    }
    for (int i = index; i < keyboardActions_.size(); ++i) keyboardActions_[i]->hide();
}

void ChallengeDashboardWidget::leaveEvent(QEvent*)
{
    if (!hoverKey_.isEmpty()) {
        hoverKey_.clear();
        unsetCursor();
        QToolTip::hideText();
        update();
    }
}

void ChallengeDashboardWidget::rebuildCursor(const QPoint& pos)
{
    const HitRegion* hit = hitAt(pos);
    const QString nextKey = hit ? hitKey(hit->role, hit->index) : QString();
    if (nextKey != hoverKey_) {
        hoverKey_ = nextKey;
        update();
    }
    if (hit) {
        setCursor(Qt::PointingHandCursor);
        if (!hit->tooltip.isEmpty()) {
            QToolTip::showText(mapToGlobal(pos + QPoint(12, 18)), hit->tooltip, this);
        }
    } else {
        unsetCursor();
        QToolTip::hideText();
    }
}

const ChallengeDashboardWidget::HitRegion* ChallengeDashboardWidget::hitAt(const QPoint& pos) const
{
    for (int i = hitRegions_.size() - 1; i >= 0; --i) {
        if (hitRegions_[i].rect.contains(pos)) {
            return &hitRegions_[i];
        }
    }
    return nullptr;
}

void ChallengeDashboardWidget::addHit(const QRectF& rect, HitRole role, int index, const QString& tooltip) const
{
    hitRegions_.append({rect, role, index, tooltip});
}

QString ChallengeDashboardWidget::hitKey(HitRole role, int index) const
{
    return QStringLiteral("%1:%2").arg(static_cast<int>(role)).arg(index);
}

bool ChallengeDashboardWidget::isHovered(HitRole role, int index) const
{
    return hoverKey_ == hitKey(role, index);
}

int ChallengeDashboardWidget::completionPercent(int progress, int target) const
{
    if (target <= 0) return 0;
    return static_cast<int>(std::clamp(progress * 100.0 / target, 0.0, 100.0));
}

void ChallengeDashboardWidget::claimMonthlyReward(const DataModel& data)
{
    if (data.monthlyState == RewardState::NotReady) {
        challengeFeedback(this, QStringLiteral("本月挑战"),
                                 QStringLiteral("本月挑战尚未完成，完成后即可领取金币奖励。"));
        return;
    }
    if (data.monthlyState == RewardState::Claimed) {
        challengeFeedback(this, QStringLiteral("本月挑战"), QStringLiteral("本月挑战奖励已领取。"));
        return;
    }
    claimReward(ChallengeRewardService::Kind::Monthly, 0, data.date, QStringLiteral("领取成功"),
                QStringLiteral("已领取本月挑战奖励：🪙 %1。植物可在商城查看与解锁。").arg(data.monthlyCoins));
}

void ChallengeDashboardWidget::claimCheckinReward(const DataModel& data, int index)
{
    if (index < 0 || index >= data.checkins.size()) return;
    const auto reward = data.checkins[index];
    if (reward.state == RewardState::Available || reward.state == RewardState::Unavailable) {
        claimReward(ChallengeRewardService::Kind::Checkin, index, data.date, QStringLiteral("签到成功"),
                    QStringLiteral("已领取今日签到奖励：%1 %2。").arg(reward.icon, reward.text));
    } else if (reward.state == RewardState::Claimed) {
        challengeFeedback(this, QStringLiteral("每日签到"), QStringLiteral("今日奖励已领取。"));
    } else if (reward.state == RewardState::Locked) {
        challengeFeedback(this, QStringLiteral("奖励预览"),
                                 QStringLiteral("该奖励将在后续签到日开放。奖励内容：%1 %2。").arg(reward.icon, reward.text));
    } else {
        challengeFeedback(this, QStringLiteral("无法领取"), QStringLiteral("这个签到奖励已过期，当前版本不支持补签。"), QMessageBox::Warning);
    }
}

void ChallengeDashboardWidget::claimTaskReward(const DataModel& data, int index)
{
    if (index < 0 || index >= data.tasks.size()) return;
    const auto task = data.tasks[index];
    if (task.state == RewardState::NotReady) {
        showTaskRewardPreview(task);
        return;
    }
    if (task.state == RewardState::Claimed) {
        challengeFeedback(this, QStringLiteral("每日挑战"), QStringLiteral("奖励已领取。"));
        return;
    }
    claimReward(ChallengeRewardService::Kind::Daily, index, data.date, QStringLiteral("领取成功"),
                QStringLiteral("已领取「%1」奖励：%2 x%3。")
                    .arg(task.title, task.rewardIcon).arg(task.rewardCoins));
}

void ChallengeDashboardWidget::claimReward(ChallengeRewardService::Kind kind, int index, const QDate& date,
                                         const QString& title, const QString& successText)
{
    if (!rewardService_) {
        challengeFeedback(this, QStringLiteral("奖励暂不可用"), QStringLiteral("尚未绑定当前账户的奖励服务。"), QMessageBox::Warning);
        return;
    }
    const auto result = rewardService_->claim(kind, index, date, challenges_, records_);
    update();
    if (result.status == ChallengeRewardService::Status::Applied) {
        emit rewardsChanged(result.coins);
        challengeFeedback(this, title, successText);
    } else if (result.status == ChallengeRewardService::Status::AlreadyClaimed) {
        challengeFeedback(this, QStringLiteral("奖励已领取"), QStringLiteral("该奖励已领取，无需重复操作。"));
    } else {
        challengeFeedback(this, QStringLiteral("奖励未领取"), result.error, QMessageBox::Warning);
    }
}

void ChallengeDashboardWidget::showResourceDialog(HitRole role, const DataModel& data)
{
    if (role == HitRole::Coin) {
        const bool openShop = DialogPresenter::confirm(this, QStringLiteral("金币详情"),
            QStringLiteral("当前金币：%1\n金币可通过完成专注、签到、成就和挑战奖励获得。\n是否前往植物商城？")
                .arg(coins_));
        if (openShop) emit openShopRequested();
    } else if (role == HitRole::Growth) {
        challengeFeedback(this, QStringLiteral("成长值说明"),
            QStringLiteral("本月成长值：%1\n本月成功专注的有效整分钟 × 2，加植物成长阶段 × 6。每日成长任务只统计今日。")
                .arg(data.rewardGrowth));
    }
}

void ChallengeDashboardWidget::showMonthlyDialog(const DataModel& data)
{
    challengeFeedback(this, QStringLiteral("本月挑战详情"),
        QStringLiteral("挑战周期：%1 至 %2\n挑战目标：累计成功专注 %3 分钟\n当前进度：%4 分钟（%5%）\n完成奖励：🪙 %6\n主题植物：%7（商城解锁，不由此挑战发放）\n规则说明：按专注开始日归属，累计有效秒数后换算整分钟。本月结束前达到目标即可领取。")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyy年M月1日")))
            .arg(QDate(QDate::currentDate().year(), QDate::currentDate().month(), QDate::currentDate().daysInMonth()).toString(QStringLiteral("yyyy年M月d日")))
            .arg(data.monthTarget)
            .arg(std::min(data.monthMinutes, data.monthTarget))
            .arg(completionPercent(data.monthMinutes, data.monthTarget))
            .arg(data.monthlyCoins).arg(data.rewardPlant.name));
}

void ChallengeDashboardWidget::showRefreshDialog(bool monthly) const
{
    if (monthly) {
        const QDate today = QDate::currentDate();
        const QDate end(today.year(), today.month(), today.daysInMonth());
        challengeFeedback(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("本月挑战刷新规则"),
            QStringLiteral("本月挑战在自然月结束时刷新。\n本期结束时间：%1 23:59。\n未完成的进度不会带入下月，已领取奖励会保留。")
                .arg(end.toString(QStringLiteral("yyyy年M月d日"))));
    } else {
        challengeFeedback(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("每日刷新规则"),
            QStringLiteral("每日签到和每日挑战会在本地日期切换后自动刷新。\n当前版本不支持补签；未领取的已完成任务奖励在刷新后不保留。"));
    }
}

void ChallengeDashboardWidget::showPlantDialog(const PlantInfo& plant)
{
    QDialog dialog(this);
    DialogPresenter::prepare(dialog);
    dialog.setWindowTitle(QStringLiteral("植物详情"));
    dialog.setFixedWidth(460);
    dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
        "QDialog#forestDialog QLabel#challengePlantDetailTitle { font-size:20px; font-weight:700; }"));
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 22, 24, 20);
    layout->setSpacing(12);

    auto* icon = new QLabel(&dialog);
    icon->setAlignment(Qt::AlignCenter);
    QPixmap pix = PlantImageUtils::loadPlantIcon(plant.iconPath);
    if (!pix.isNull()) {
        icon->setPixmap(pix.scaled(130, 130, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    layout->addWidget(icon);

    auto* title = new QLabel(QStringLiteral("%1  ·  %2").arg(plant.name, plant.rarity), &dialog);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("challengePlantDetailTitle");
    layout->addWidget(title);

    auto* body = new QLabel(QStringLiteral("%1\n\n解锁条件：%2\n获取方式：%3")
                                .arg(plant.description, plant.condition, plant.source), &dialog);
    body->setWordWrap(true);
    layout->addWidget(body);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* shop = new QPushButton(QStringLiteral("前往植物商城查看"), &dialog);
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    DialogPresenter::setPrimary(shop);
    DialogPresenter::setSecondary(close);
    buttons->addWidget(shop);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(shop, &QPushButton::clicked, &dialog, [&]() {
        dialog.accept();
        emit openShopRequested();
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.adjustSize();
    dialog.exec();
}

void ChallengeDashboardWidget::showTaskDialog(const DataModel& data, int index)
{
    if (index < 0 || index >= data.tasks.size()) return;
    const auto task = data.tasks[index];
    const QString message = QStringLiteral("%1\n\n").arg(task.title) + QStringLiteral(
        "任务类型：%1\n完成条件：%2\n当前进度：%3 / %4（%5%）\n奖励：%6 x%7\n刷新规则：每日随本地日期切换自动刷新。")
        .arg(task.type, task.description)
        .arg(std::min(task.progress, task.target))
        .arg(task.target)
        .arg(completionPercent(task.progress, task.target))
        .arg(task.rewardIcon)
        .arg(task.rewardCoins);
    const bool actionChosen = DialogPresenter::detailWithAction(
        this, QStringLiteral("任务详情"), message, task.actionText);
    if (actionChosen) {
        routeTaskAction(task);
    }
}

void ChallengeDashboardWidget::showTaskRewardPreview(const TaskItem& task) const
{
    QDialog dialog(const_cast<ChallengeDashboardWidget*>(this));
    DialogPresenter::prepare(dialog);
    dialog.setWindowTitle(QStringLiteral("奖励预览"));
    dialog.setFixedWidth(300);
    dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
        "QDialog#forestDialog QLabel#challengeRewardPreviewIcon { font-size:54px; }"
        "QDialog#forestDialog QLabel#challengeRewardPreviewAmount { font-size:24px; font-weight:700; }"));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(8);

    auto* icon = new QLabel(task.rewardIcon, &dialog);
    icon->setAlignment(Qt::AlignCenter);
    icon->setObjectName("challengeRewardPreviewIcon");
    layout->addWidget(icon);

    auto* amount = new QLabel(QStringLiteral("x%1").arg(task.rewardCoins), &dialog);
    amount->setAlignment(Qt::AlignCenter);
    amount->setObjectName("challengeRewardPreviewAmount");
    layout->addWidget(amount);

    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    DialogPresenter::setSecondary(close);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(close);
    dialog.adjustSize();
    dialog.exec();
}

void ChallengeDashboardWidget::showPlusDialog(const TaskItem& task) const
{
    challengeFeedback(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("加成奖励说明"),
        QStringLiteral("「%1」的金币奖励已包含加成，共 🪙 %2。\n当前版本不需要会员，不另外发放植物或道具。\n任务完成后点击宝箱，一次领取全部奖励。")
            .arg(task.title).arg(task.rewardCoins));
}

void ChallengeDashboardWidget::routeTaskAction(const TaskItem& task)
{
    if (task.taskType == ChallengeRewardService::TaskType::PlantMinutes) {
        for (const auto& plant : shopPlants()) {
            if (plant.type == task.plantType) { showPlantDialog(plant); return; }
        }
    } else if (task.taskType == ChallengeRewardService::TaskType::Growth) {
        emit growthRulesRequested();
    } else {
        emit openFocusRequested();
    }
}

void ChallengeDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    const ChallengeLayout metrics;
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(challengeFont(24, QFont::Bold));
    painter.drawText(QRectF(rect.left(), rect.top(), rect.width() - 360, metrics.title),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("挑战中心"));
    painter.setFont(challengeFont(12));
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    painter.drawText(QRectF(rect.left(), rect.top() + metrics.title + 8, rect.width() - 360, metrics.small),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("本地挑战 · 不跨设备同步"));

    const QFontMetrics resourceFont(challengeFont(16, QFont::DemiBold));
    const qreal pillW = std::max(112, resourceFont.horizontalAdvance(QString::number(std::max(coins_,
        static_cast<uint32_t>(std::max(0, data.rewardGrowth))))) + 62);
    const qreal pillH = std::max<qreal>(40, resourceFont.height() + 16);
    QRectF growth(rect.right() - pillW, rect.top(), pillW, pillH);
    QRectF coin(growth.left() - pillW - 12, rect.top(), pillW, pillH);
    drawResourcePill(painter, coin, QStringLiteral("🪙"), QString::number(coins_), isHovered(HitRole::Coin));
    drawResourcePill(painter, growth, QStringLiteral("🌿"), QString::number(data.rewardGrowth), isHovered(HitRole::Growth));
    addHit(coin, HitRole::Coin, -1, QStringLiteral("查看金币详情或进入植物商城"));
    addHit(growth, HitRole::Growth, -1, QStringLiteral("查看成长值计算规则"));
    painter.restore();
}

void ChallengeDashboardWidget::drawMonthlyCard(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    const ChallengeLayout metrics;
    QLinearGradient card(rect.topLeft(), rect.topRight());
    card.setColorAt(0, QColor("#134F48"));
    card.setColorAt(1, QColor("#247C70"));
    painter.setPen(isHovered(HitRole::MonthlyCard) ? QPen(QColor("#92C5B2"), 2) : Qt::NoPen);
    painter.setBrush(card);
    painter.drawRoundedRect(rect, 8, 8);
    addHit(rect, HitRole::MonthlyCard, -1, QStringLiteral("打开本月挑战详情"));

    const qreal plantW = rect.width() < 900 ? 156 : 204;
    const qreal plantLeft = rect.right() - plantW - 24;
    const qreal textW = plantLeft - rect.left() - 48;
    qreal y = rect.top() + 20;
    painter.setPen(Qt::white);
    painter.setFont(challengeFont(24, QFont::Bold));
    painter.drawText(QRectF(rect.left() + 24, y, textW, metrics.title),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("本月挑战"));
    y += metrics.title + 8;
    painter.setFont(challengeFont(14));
    painter.setPen(QColor("#D8EAE3"));
    painter.drawText(QRectF(rect.left() + 24, y, textW, metrics.body),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("累计成功专注，完成目标即可领取金币"));
    y += metrics.body + 14;
    QRectF bar(rect.left() + 24, y, textW, 10);
    drawProgressBar(painter, bar, data.monthMinutes, data.monthTarget, QColor("#8CCEB7"));
    addHit(bar.adjusted(0, -4, 0, 4), HitRole::MonthlyProgress, -1,
           QStringLiteral("当前 %1 / 目标 %2，完成 %3%")
               .arg(std::min(data.monthMinutes, data.monthTarget)).arg(data.monthTarget)
               .arg(completionPercent(data.monthMinutes, data.monthTarget)));
    y = bar.bottom() + 8;
    painter.setPen(Qt::white);
    painter.setFont(challengeFont(14, QFont::DemiBold));
    painter.drawText(QRectF(bar.left(), y, bar.width(), metrics.body), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("%1 / %2 分钟").arg(std::min(data.monthMinutes, data.monthTarget)).arg(data.monthTarget));

    const qreal footerH = metrics.body + 12;
    QRectF claim(rect.left() + 24, rect.bottom() - footerH - 16,
                 QFontMetrics(challengeFont(14, QFont::Bold)).horizontalAdvance(QStringLiteral("领取奖励")) + 32, footerH);
    const bool available = data.monthlyState == RewardState::Available;
    drawButton(painter, claim, data.monthlyState == RewardState::Claimed ? QStringLiteral("已领取")
        : available ? QStringLiteral("领取奖励") : QStringLiteral("未完成"),
        QColor("#F3F7EE"), available, isHovered(HitRole::MonthlyReward));
    addHit(claim, HitRole::MonthlyReward, -1, !data.rewardError.isEmpty() ? data.rewardError
        : data.monthlyState == RewardState::Claimed ? QStringLiteral("本月奖励已领取")
        : available ? QStringLiteral("领取本月挑战奖励") : QStringLiteral("挑战完成后可领取奖励"));
    QRectF time(claim.right() + 20, claim.top(), textW - claim.width() - 20, footerH);
    painter.setFont(challengeFont(14));
    painter.setPen(isHovered(HitRole::MonthlyTime) ? Qt::white : QColor("#D8EAE3"));
    painter.drawText(time, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("⏱ 剩余 %1 天").arg(data.remainingDays));
    addHit(time, HitRole::MonthlyTime, -1, QStringLiteral("查看本月挑战刷新规则"));

    QRectF plant(plantLeft, rect.top() + 12, plantW, rect.height() - metrics.body - 38);
    drawRewardPlant(painter, plant, data.rewardPlant);
    QRectF name(plantLeft, rect.bottom() - metrics.body - 20, plantW, metrics.body);
    painter.setPen(Qt::white);
    painter.setFont(challengeFont(14, QFont::DemiBold));
    painter.drawText(name, Qt::AlignCenter, data.rewardPlant.name);
    addHit(plant.united(name), HitRole::MonthlyPlant, -1, QStringLiteral("查看植物详情"));
    painter.restore();
}

void ChallengeDashboardWidget::drawCheckinCard(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    const ChallengeLayout metrics;
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(challengeFont(18, QFont::Bold));
    painter.drawText(QRectF(rect.left(), rect.top(), rect.width() - 240, metrics.section),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("每日签到"));
    QRectF refresh(rect.right() - 240, rect.top(), 240, metrics.section);
    painter.setFont(challengeFont(12));
    painter.setPen(AppStyle::color(isHovered(HitRole::CheckinRefresh) ? AppStyle::Color::Primary : AppStyle::Color::Muted));
    painter.drawText(refresh, Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("⏱ %1 小时后刷新").arg(24 - QTime::currentTime().hour()));
    addHit(refresh, HitRole::CheckinRefresh, -1, QStringLiteral("查看每日签到刷新规则"));

    const int count = std::max(1, static_cast<int>(data.checkins.size()));
    const qreal boxW = (rect.width() - 12 * (count - 1)) / count;
    for (int i = 0; i < data.checkins.size(); ++i) {
        const auto& reward = data.checkins[i];
        QRectF box(rect.left() + i * (boxW + 12), rect.top() + metrics.sectionOffset(),
                   boxW, rect.height() - metrics.sectionOffset());
        painter.setBrush(AppStyle::color(reward.state == RewardState::Available || reward.state == RewardState::Claimed
            ? AppStyle::Color::Selection : AppStyle::Color::Surface));
        painter.setPen(QPen(AppStyle::color(isHovered(HitRole::CheckinReward, i)
            ? AppStyle::Color::Primary : AppStyle::Color::Border), 1));
        painter.drawRoundedRect(box, 8, 8);
        painter.setPen(AppStyle::color(AppStyle::Color::Text));
        painter.setFont(challengeFont(28));
        qreal y = box.top() + 8;
        painter.drawText(QRectF(box.left(), y, box.width(), metrics.icon), Qt::AlignCenter, reward.icon);
        y += metrics.icon + 2;
        painter.setFont(challengeFont(14, QFont::DemiBold));
        painter.drawText(QRectF(box.left(), y, box.width(), metrics.body), Qt::AlignCenter, reward.text);
        y += metrics.body + 2;
        painter.setFont(challengeFont(12));
        painter.setPen(AppStyle::color(AppStyle::Color::Muted));
        painter.drawText(QRectF(box.left(), y, box.width(), metrics.small), Qt::AlignCenter, checkinStateText(reward.state));
        addHit(box, HitRole::CheckinReward, i,
               QStringLiteral("%1 · 金币 %2").arg(checkinStateText(reward.state)).arg(reward.coinReward));
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawTaskPanel(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    const ChallengeLayout metrics;
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(challengeFont(18, QFont::Bold));
    painter.drawText(QRectF(rect.left(), rect.top(), rect.width() - 240, metrics.section),
                     Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("每日挑战"));
    QRectF refresh(rect.right() - 240, rect.top(), 240, metrics.section);
    painter.setPen(AppStyle::color(isHovered(HitRole::CheckinRefresh, 99) ? AppStyle::Color::Primary : AppStyle::Color::Muted));
    painter.setFont(challengeFont(12));
    painter.drawText(refresh, Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("⏱ 今日自动刷新"));
    addHit(refresh, HitRole::CheckinRefresh, 99, QStringLiteral("查看每日挑战刷新规则"));

    const int columns = rect.width() < 900 ? 1 : 2;
    const qreal cardW = (rect.width() - 16 * (columns - 1)) / columns;
    for (int i = 0; i < data.tasks.size(); ++i) {
        QRectF task(rect.left() + (i % columns) * (cardW + 16),
                    rect.top() + metrics.sectionOffset() + (i / columns) * (metrics.taskHeight() + 16),
                    cardW, metrics.taskHeight());
        drawTaskCard(painter, task, data.tasks[i], i);
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawTaskCard(QPainter& painter, const QRectF& rect, const TaskItem& task, int index) const
{
    painter.save();
    const ChallengeLayout metrics;
    QLinearGradient background(rect.topLeft(), rect.bottomRight());
    background.setColorAt(0, QColor("#EDF5F7"));
    background.setColorAt(1, AppStyle::color(AppStyle::Color::Surface));
    painter.setBrush(background);
    painter.setPen(QPen(AppStyle::color(isHovered(HitRole::TaskCard, index)
        ? AppStyle::Color::Primary : AppStyle::Color::Border), 1));
    painter.drawRoundedRect(rect, 8, 8);
    addHit(rect, HitRole::TaskCard, index, task.title + QStringLiteral(" · 打开任务详情"));

    const qreal rewardW = std::max<qreal>(68, QFontMetrics(challengeFont(12)).horizontalAdvance(QStringLiteral("已领取")) + 20);
    const qreal arrowW = task.arrow ? 32 : 0;
    QRectF reward(rect.right() - 16 - rewardW - arrowW, rect.top() + 12, rewardW, 48 + metrics.small + 4);
    const qreal textW = reward.left() - rect.left() - 32;
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.setFont(challengeFont(14, QFont::Bold));
    painter.drawText(QRectF(rect.left() + 16, rect.top() + 14, textW, metrics.body * 2),
                     Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, task.title);
    painter.setFont(challengeFont(12));
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    const qreal categoryY = rect.top() + 14 + metrics.body * 2 + 4;
    painter.drawText(QRectF(rect.left() + 16, categoryY, textW, metrics.small),
                     Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(painter.font()).elidedText(
                         task.type + QStringLiteral(" · ") + checkinStateText(task.state), Qt::ElideRight, qFloor(textW)));

    QRectF chest(reward.center().x() - 24, reward.top(), 48, 48);
    drawChest(painter, chest, isHovered(HitRole::TaskChest, index) ? task.chestColor.lighter(110) : task.chestColor);
    painter.setFont(challengeFont(12, QFont::DemiBold));
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.drawText(QRectF(reward.left(), chest.bottom() + 4, reward.width(), metrics.small), Qt::AlignCenter,
                     task.state == RewardState::Claimed ? QStringLiteral("已领取") : QStringLiteral("x%1").arg(task.rewardCoins));
    if (task.state == RewardState::Available) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(AppStyle::color(AppStyle::Color::Primary), 2));
        painter.drawRoundedRect(reward.adjusted(-2, -2, 2, 2), 8, 8);
    }
    addHit(reward, HitRole::TaskChest, index, task.state == RewardState::Available ? QStringLiteral("领取奖励")
        : task.state == RewardState::Claimed ? QStringLiteral("奖励已领取")
        : !rewardService_ ? QStringLiteral("奖励服务未绑定") : QStringLiteral("未完成 · 查看奖励预览"));
    if (task.arrow) {
        QRectF arrow(rect.right() - 42, rect.top() + 18, 32, 40);
        painter.setFont(challengeFont(28));
        painter.setPen(AppStyle::color(isHovered(HitRole::TaskArrow, index) ? AppStyle::Color::Primary : AppStyle::Color::Muted));
        painter.drawText(arrow, Qt::AlignCenter, QString::fromUtf8(u8"›"));
        addHit(arrow, HitRole::TaskArrow, index, task.actionText);
    }

    QRectF bar(rect.left() + 16, categoryY + metrics.small + 14, rect.width() - 32, 10);
    drawProgressBar(painter, bar, task.progress, task.target, AppStyle::color(AppStyle::Color::Primary));
    addHit(bar.adjusted(0, -4, 0, 4), HitRole::TaskProgress, index,
           QStringLiteral("当前 %1 / 目标 %2，完成 %3%").arg(std::min(task.progress, task.target))
               .arg(task.target).arg(completionPercent(task.progress, task.target)));
    const qreal footerY = bar.bottom() + 8;
    painter.setFont(challengeFont(12, QFont::DemiBold));
    painter.setPen(AppStyle::color(AppStyle::Color::Muted));
    painter.drawText(QRectF(rect.left() + 16, footerY, rect.width() - 32, metrics.small),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("%1 / %2").arg(std::min(task.progress, task.target)).arg(task.target));
    if (task.plus) {
        const qreal plusW = QFontMetrics(challengeFont(12)).horizontalAdvance(QStringLiteral("加成奖励")) + 16;
        QRectF plus(rect.left() + 16, footerY - 2, plusW, metrics.small + 4);
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppStyle::color(AppStyle::Color::Selection));
        painter.drawRoundedRect(plus, 4, 4);
        painter.setPen(AppStyle::color(AppStyle::Color::Primary));
        painter.drawText(plus, Qt::AlignCenter, QStringLiteral("加成奖励"));
        addHit(plus, HitRole::TaskPlus, index, QStringLiteral("查看加成奖励说明"));
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawResourcePill(QPainter& painter, const QRectF& rect,
                                                const QString& icon, const QString& value,
                                                bool hovered) const
{
    painter.save();
    painter.setPen(QPen(AppStyle::color(hovered ? AppStyle::Color::Primary : AppStyle::Color::Border), 1));
    painter.setBrush(AppStyle::color(hovered ? AppStyle::Color::Selection : AppStyle::Color::Surface));
    painter.drawRoundedRect(rect, 8, 8);
    painter.setFont(challengeFont(16, QFont::DemiBold));
    painter.setPen(AppStyle::color(AppStyle::Color::Text));
    painter.drawText(QRectF(rect.left() + 10, rect.top(), 30, rect.height()), Qt::AlignCenter, icon);
    painter.drawText(QRectF(rect.left() + 44, rect.top(), rect.width() - 54, rect.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, value);
    painter.restore();
}

void ChallengeDashboardWidget::drawRewardPlant(QPainter& painter, const QRectF& rect, const PlantInfo& plant) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    QPixmap pix = PlantImageUtils::loadPlantIcon(plant.iconPath);
    if (!pix.isNull()) {
        const QSize target = pix.size().scaled(QSize(static_cast<int>(rect.width() * 0.72), static_cast<int>(rect.height() * 0.88)),
                                               Qt::KeepAspectRatio);
        QRect drawRect(QPoint(static_cast<int>(rect.center().x() - target.width() / 2),
                              static_cast<int>(rect.center().y() - target.height() / 2 - 2)), target);
        painter.drawPixmap(drawRect, pix);
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawChest(QPainter& painter, const QRectF& rect, const QColor& color) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, 190));
    painter.drawEllipse(rect.adjusted(-5, -5, 5, 5));
    painter.setBrush(color);
    painter.drawRoundedRect(rect.adjusted(6, 24, -6, -6), 8, 8);
    painter.setBrush(color.lighter(130));
    painter.drawRoundedRect(QRectF(rect.left() + 9, rect.top() + 14, rect.width() - 18, 24), 10, 10);
    painter.setBrush(QColor("#FFE36A"));
    painter.drawRect(QRectF(rect.center().x() - 5, rect.top() + 14, 10, rect.height() - 20));
    painter.setBrush(QColor("#F7FFF7"));
    painter.drawEllipse(QRectF(rect.center().x() - 6, rect.center().y() + 4, 12, 12));
    painter.setPen(QPen(QColor("#7EC5E8"), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(rect.center().x(), rect.top() + 15), QPointF(rect.center().x() - 16, rect.top() + 3));
    painter.drawLine(QPointF(rect.center().x(), rect.top() + 15), QPointF(rect.center().x() + 16, rect.top() + 3));
    painter.restore();
}

void ChallengeDashboardWidget::drawProgressBar(QPainter& painter, const QRectF& rect, int progress,
                                               int target, const QColor& fill) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#EDF0EF"));
    painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    const qreal ratio = target > 0 ? std::clamp(static_cast<qreal>(progress) / target, 0.0, 1.0) : 0.0;
    QRectF filled = rect.adjusted(3, 3, -3, -3);
    filled.setWidth(filled.width() * ratio);
    painter.setBrush(fill);
    if (ratio > 0) painter.drawRoundedRect(filled, filled.height() / 2, filled.height() / 2);
    painter.restore();
}

void ChallengeDashboardWidget::drawButton(QPainter& painter, const QRectF& rect, const QString& text,
                                          const QColor& color, bool enabled, bool hovered) const
{
    painter.save();
    QColor bg = enabled ? color : QColor(255, 255, 255, 58);
    if (enabled && hovered) bg = bg.lighter(112);
    painter.setBrush(bg);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    painter.setPen(enabled ? AppStyle::color(AppStyle::Color::Primary) : QColor("#E5F0EB"));
    painter.setFont(challengeFont(14, QFont::Bold));
    painter.drawText(rect, Qt::AlignCenter, text);
    painter.restore();
}
