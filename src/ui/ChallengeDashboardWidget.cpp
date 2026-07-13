#include "ui/ChallengeDashboardWidget.h"

#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"

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
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QTime>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

ChallengeDashboardWidget::ChallengeDashboardWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("challengeDashboard");
    setMinimumSize(720, 1040);
    setAttribute(Qt::WA_StyledBackground, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    resetDailyStateIfNeeded();
}

void ChallengeDashboardWidget::setSnapshot(uint32_t coins,
                                           const QVector<Challenge>& challenges,
                                           const std::vector<FocusRecord>& records)
{
    coins_ = coins;
    challenges_ = challenges;
    records_ = records;
    resetDailyStateIfNeeded();
    update();
}

void ChallengeDashboardWidget::resetDailyStateIfNeeded()
{
    const QDate today = QDate::currentDate();
    const QString currentMonthKey = today.toString(QStringLiteral("yyyyMM"));
    if (stateDate_ == today && monthKey_ == currentMonthKey &&
        taskClaimed_.size() == 4 && checkinClaimed_.size() == 5) {
        return;
    }
    loadRewardState(today);
}

void ChallengeDashboardWidget::loadRewardState(const QDate& today)
{
    stateDate_ = today;
    monthKey_ = today.toString(QStringLiteral("yyyyMM"));

    const auto state = UserPreferences::instance().challengeRewardState(today, 4, 5);
    taskClaimed_ = state.taskClaimed;
    checkinClaimed_ = state.checkinClaimed;
    monthlyClaimed_ = state.monthlyClaimed;
}

void ChallengeDashboardWidget::saveRewardState() const
{
    const QDate date = stateDate_.isValid() ? stateDate_ : QDate::currentDate();
    UserPreferences::ChallengeRewardState state;
    state.date = date;
    state.monthKey = monthKey_.isEmpty() ? date.toString(QStringLiteral("yyyyMM")) : monthKey_;
    state.taskClaimed = taskClaimed_;
    state.checkinClaimed = checkinClaimed_;
    state.monthlyClaimed = monthlyClaimed_;
    UserPreferences::instance().saveChallengeRewardState(state);
}

QString ChallengeDashboardWidget::boolVectorToString(const QVector<bool>& values)
{
    QString result;
    result.reserve(values.size());
    for (bool value : values) {
        result.append(value ? QLatin1Char('1') : QLatin1Char('0'));
    }
    return result;
}

QVector<bool> ChallengeDashboardWidget::boolVectorFromString(const QString& text, int size)
{
    QVector<bool> values(size, false);
    for (int i = 0; i < size && i < text.size(); ++i) {
        values[i] = text.at(i) == QLatin1Char('1');
    }
    return values;
}

QVector<ChallengeDashboardWidget::PlantInfo> ChallengeDashboardWidget::shopPlants() const
{
    QVector<PlantInfo> plants;
    plants.reserve(PlantCatalog::all().size());
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        PlantInfo info;
        info.name = plant.displayName;
        info.rarity = plant.storeCost >= 1000 ? QStringLiteral("稀有树种") : QStringLiteral("普通树种");
        info.description = QStringLiteral("可在植物商城解锁，并用于专注与挑战奖励展示。");
        info.condition = plant.type == 0 ? QStringLiteral("默认可用")
                                         : QStringLiteral("在植物商城使用金币解锁");
        info.source = QStringLiteral("植物商城");
        info.iconPath = plant.iconPath;
        info.type = plant.type;
        plants.push_back(info);
    }
    return plants;
}

ChallengeDashboardWidget::DataModel ChallengeDashboardWidget::buildData() const
{
    DataModel data;
    const QDate today = QDate::currentDate();
    const QDate monthStart(today.year(), today.month(), 1);
    const int monthDays = today.daysInMonth();
    const auto plants = shopPlants();
    const int plantIndex = plants.isEmpty() ? 0 : today.month() % plants.size();
    const uint32_t selectedPlant = plants.isEmpty() ? 0 : plants[plantIndex].type;
    int selectedPlantMinutes = 0;

    for (const auto& record : records_) {
        if (record.status != FocusRecordStatus::Success) continue;
        const QDate d = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startTimestamp)).date();
        const int minutes = std::max(1, static_cast<int>((record.actualSeconds + 59) / 60));
        if (d == today) {
            data.todayMinutes += minutes;
            data.todaySessions += 1;
            if (record.plantType % 6 == selectedPlant) {
                selectedPlantMinutes += minutes;
            }
        }
        if (d >= monthStart && d <= today) {
            data.monthMinutes += minutes;
        }
    }

    data.monthTarget = std::max(1, monthDays * 25);
    data.remainingDays = std::max(0, static_cast<int>(today.daysTo(QDate(today.year(), today.month(), monthDays))));
    data.rewardPlant = plants.isEmpty() ? PlantInfo{} : plants[plantIndex];

    for (const auto& c : challenges_) {
        if (c.status == ChallengeStatus::PENDING || c.status == ChallengeStatus::ACCEPTED) {
            const int progress = static_cast<int>(std::max(c.creatorProgress, c.targetProgress));
            data.monthTarget = std::max(1, static_cast<int>(c.targetMinutes));
            data.monthMinutes = std::max(data.monthMinutes, progress);
            data.remainingDays = std::max(0, static_cast<int>(today.daysTo(c.endDate)));
            if (!plants.isEmpty()) {
                data.rewardPlant = plants[static_cast<int>(c.targetMinutes % plants.size())];
            }
            break;
        }
    }

    data.rewardGrowth = data.monthMinutes * 2 + data.todaySessions * 8;

    const int currentCheckin = (today.day() - 1) % 5;
    constexpr int checkinCoinRewards[] = {10, 14, 18, 22, 30};
    for (int i = 0; i < 5; ++i) {
        CheckinReward reward;
        reward.icon = QStringLiteral("🪙");
        reward.coinReward = checkinCoinRewards[i];
        reward.text = QStringLiteral("x%1").arg(reward.coinReward);

        if (i == currentCheckin) {
            reward.state = checkinClaimed_.value(i, false) ? QStringLiteral("已领取") : QStringLiteral("今日可领取");
        } else if (i < currentCheckin) {
            reward.state = QStringLiteral("过期");
        } else {
            reward.state = QStringLiteral("未解锁");
        }
        data.checkins.append(reward);
    }

    const int taskMinuteTarget = std::max(25, data.monthTarget / 20);
    const int treeMinuteTarget = 30 + (today.day() % 4) * 10;
    const int growthTarget = 120 + static_cast<int>(challenges_.size()) * 15;
    data.tasks = {
        {QStringLiteral("种植任意树种若干分钟"), QStringLiteral("每日专注"),
         QStringLiteral("完成任意植物的专注计时，累计达到目标分钟数。"),
         QStringLiteral("🪙"), QStringLiteral("去专注"),
         data.todayMinutes, taskMinuteTarget, 12, QColor("#FFC93D"), true, true},
        {QStringLiteral("完成一次专注"), QStringLiteral("基础任务"),
         QStringLiteral("成功完成任意一次专注后即可领取奖励。"),
         QStringLiteral("🪙"), QStringLiteral("去专注"),
         data.todaySessions, 1, 8, QColor("#6AA9E9"), false, true},
        {QStringLiteral("种植指定树种若干分钟"), QStringLiteral("树种任务"),
         QStringLiteral("使用本月奖励植物对应的树种进行专注，累计达到目标分钟数。"),
         QStringLiteral("🪙"), QStringLiteral("去选择树种"),
         selectedPlantMinutes, treeMinuteTarget, 16, QColor("#B874F0"), true, true},
        {QStringLiteral("累计获得成长值"), QStringLiteral("成长任务"),
         QStringLiteral("通过今日专注和挑战进度累计成长值。"),
         QStringLiteral("🪙"), QStringLiteral("查看规则"),
         data.rewardGrowth, growthTarget, 10, QColor("#F4A84A"), false, true}
    };

    return data;
}

void ChallengeDashboardWidget::paintEvent(QPaintEvent*)
{
    hitRegions_.clear();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QLinearGradient bg(rect().topLeft(), rect().bottomLeft());
    bg.setColorAt(0.0, QColor("#C8EFE1"));
    bg.setColorAt(0.58, QColor("#DFF4EC"));
    bg.setColorAt(1.0, QColor("#C7E8F5"));
    painter.fillRect(rect(), bg);

    const DataModel data = buildData();
    const qreal margin = 30;
    QRectF content = QRectF(rect()).adjusted(margin, 24, -margin, -28);

    QRectF header(content.left(), content.top(), content.width(), 156);
    drawHeader(painter, header, data);

    QRectF monthly(content.left() + 26, header.top() + 76, content.width() - 52, 214);
    drawMonthlyCard(painter, monthly, data);

    qreal y = monthly.bottom() + 24;
    qreal gap = 22;
    const bool compactLayout = content.width() < 900;
    QRectF checkin;
    QRectF tasks;
    if (compactLayout) {
        checkin = QRectF(content.left(), y, content.width(), 196);
        tasks = QRectF(content.left(), checkin.bottom() + 18,
                       content.width(), content.bottom() - checkin.bottom() - 18);
    } else {
        qreal checkW = content.width() * 0.34;
        checkin = QRectF(content.left(), y, checkW, 196);
        tasks = QRectF(content.left() + checkW + gap, y,
                      content.width() - checkW - gap, content.bottom() - y);
    }
    drawCheckinCard(painter, checkin, data);
    drawTaskPanel(painter, tasks, data);
}

void ChallengeDashboardWidget::mouseMoveEvent(QMouseEvent* event)
{
    rebuildCursor(event->pos());
}

void ChallengeDashboardWidget::mouseReleaseEvent(QMouseEvent* event)
{
    resetDailyStateIfNeeded();
    const HitRegion* hit = hitAt(event->pos());
    if (!hit) return;

    const DataModel data = buildData();
    switch (hit->role) {
    case HitRole::Coin:
    case HitRole::Growth:
        showResourceDialog(hit->role, data);
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
        claimCheckinReward(data, hit->index);
        break;
    case HitRole::TaskCard:
    case HitRole::TaskProgress:
        showTaskDialog(data, hit->index);
        break;
    case HitRole::TaskChest:
        claimTaskReward(data, hit->index);
        break;
    case HitRole::TaskPlus:
        if (hit->index >= 0 && hit->index < data.tasks.size()) showPlusDialog(data.tasks[hit->index]);
        break;
    case HitRole::TaskArrow:
        if (hit->index >= 0 && hit->index < data.tasks.size()) routeTaskAction(data.tasks[hit->index]);
        break;
    default:
        break;
    }
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
    if (data.monthMinutes < data.monthTarget) {
        DialogPresenter::information(this, QStringLiteral("本月挑战"),
                                 QStringLiteral("本月挑战尚未完成，完成后即可领取植物奖励。"));
        return;
    }
    if (monthlyClaimed_) {
        DialogPresenter::information(this, QStringLiteral("本月挑战"), QStringLiteral("本月挑战奖励已领取。"));
        return;
    }
    monthlyClaimed_ = true;
    saveRewardState();
    emit rewardCoinsRequested(30);
    DialogPresenter::information(this, QStringLiteral("领取成功"),
                             QStringLiteral("已领取本月挑战奖励：%1 展示资格与 🪙 30。").arg(data.rewardPlant.name));
    update();
}

void ChallengeDashboardWidget::claimCheckinReward(const DataModel& data, int index)
{
    if (index < 0 || index >= data.checkins.size()) return;
    const auto reward = data.checkins[index];
    if (reward.state == QStringLiteral("今日可领取")) {
        checkinClaimed_[index] = true;
        saveRewardState();
        if (reward.coinReward > 0) emit rewardCoinsRequested(reward.coinReward);
        DialogPresenter::information(this, QStringLiteral("签到成功"),
                                 QStringLiteral("已领取今日签到奖励：%1 %2。").arg(reward.icon, reward.text));
        update();
    } else if (reward.state == QStringLiteral("已领取")) {
        DialogPresenter::information(this, QStringLiteral("每日签到"), QStringLiteral("今日奖励已领取。"));
    } else if (reward.state == QStringLiteral("未解锁")) {
        DialogPresenter::detail(this, QStringLiteral("奖励预览"),
                                 QStringLiteral("该奖励将在后续签到日开放。奖励内容：%1 %2。").arg(reward.icon, reward.text));
    } else {
        DialogPresenter::warning(this, QStringLiteral("无法领取"), QStringLiteral("这个签到奖励已过期，当前版本不支持补签。"));
    }
}

void ChallengeDashboardWidget::claimTaskReward(const DataModel& data, int index)
{
    if (index < 0 || index >= data.tasks.size()) return;
    const auto task = data.tasks[index];
    const bool finished = task.progress >= task.target;
    if (!finished) {
        showTaskRewardPreview(task);
        return;
    }
    if (taskClaimed_.value(index, false)) {
        DialogPresenter::information(this, QStringLiteral("每日挑战"), QStringLiteral("奖励已领取。"));
        return;
    }
    taskClaimed_[index] = true;
    saveRewardState();
    if (task.rewardCoins > 0) emit rewardCoinsRequested(task.rewardCoins);
    DialogPresenter::information(this, QStringLiteral("领取成功"),
                             QStringLiteral("已领取「%1」奖励：%2 x%3。")
                                 .arg(task.title, task.rewardIcon)
                                 .arg(task.rewardCoins));
    update();
}

void ChallengeDashboardWidget::showResourceDialog(HitRole role, const DataModel& data)
{
    if (role == HitRole::Coin) {
        const bool openShop = DialogPresenter::confirm(this, QStringLiteral("金币详情"),
            QStringLiteral("当前金币：%1\n金币可通过完成专注、签到、成就和挑战奖励获得。\n是否前往植物商城？")
                .arg(coins_));
        if (openShop) emit openShopRequested();
    } else if (role == HitRole::Growth) {
        DialogPresenter::detail(this, QStringLiteral("成长值说明"),
            QStringLiteral("当前成长值：%1\n成长值根据本月专注分钟数和今日完成次数动态计算，用于衡量森林成长。")
                .arg(data.rewardGrowth));
    }
}

void ChallengeDashboardWidget::showMonthlyDialog(const DataModel& data)
{
    DialogPresenter::detail(this, QStringLiteral("本月挑战详情"),
        QStringLiteral("挑战周期：%1 至 %2\n挑战目标：累计专注 %3 分钟\n当前进度：%4 分钟（%5%）\n完成奖励：%6 展示资格、金币奖励\n规则说明：本月结束前达到目标即可领取奖励。")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyy年M月1日")))
            .arg(QDate(QDate::currentDate().year(), QDate::currentDate().month(), QDate::currentDate().daysInMonth()).toString(QStringLiteral("yyyy年M月d日")))
            .arg(data.monthTarget)
            .arg(std::min(data.monthMinutes, data.monthTarget))
            .arg(completionPercent(data.monthMinutes, data.monthTarget))
            .arg(data.rewardPlant.name));
}

void ChallengeDashboardWidget::showRefreshDialog(bool monthly) const
{
    if (monthly) {
        const QDate today = QDate::currentDate();
        const QDate end(today.year(), today.month(), today.daysInMonth());
        DialogPresenter::detail(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("本月挑战刷新规则"),
            QStringLiteral("本月挑战在自然月结束时刷新。\n本期结束时间：%1 23:59。\n未完成的进度不会带入下月，已领取奖励会保留。")
                .arg(end.toString(QStringLiteral("yyyy年M月d日"))));
    } else {
        DialogPresenter::detail(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("每日刷新规则"),
            QStringLiteral("每日签到和每日挑战会在本地日期切换后自动刷新。\n当前版本不支持补签；未领取的已完成任务奖励在刷新后不保留。"));
    }
}

void ChallengeDashboardWidget::showPlantDialog(const PlantInfo& plant)
{
    QDialog dialog(this);
    DialogPresenter::prepare(dialog);
    dialog.setWindowTitle(QStringLiteral("植物详情"));
    DialogPresenter::prepare(dialog);
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
    title->setStyleSheet("font-size:20px; font-weight:900; color:#2B6C54;");
    layout->addWidget(title);

    auto* body = new QLabel(QStringLiteral("%1\n\n解锁条件：%2\n获取方式：%3")
                                .arg(plant.description, plant.condition, plant.source), &dialog);
    body->setWordWrap(true);
    body->setStyleSheet("font-size:14px; color:#405C55; line-height:150%;");
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
    DialogPresenter::prepare(dialog);
    dialog.setMinimumWidth(240);

    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(8);

    auto* icon = new QLabel(task.rewardIcon, &dialog);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:54px; background:transparent;");
    layout->addWidget(icon);

    auto* amount = new QLabel(QStringLiteral("x%1").arg(task.rewardCoins), &dialog);
    amount->setAlignment(Qt::AlignCenter);
    amount->setStyleSheet("font-size:24px; font-weight:900; color:#2B6C54; background:transparent;");
    layout->addWidget(amount);

    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    DialogPresenter::setSecondary(close);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    layout->addWidget(close);
    dialog.exec();
}

void ChallengeDashboardWidget::showPlusDialog(const TaskItem& task) const
{
    DialogPresenter::detail(const_cast<ChallengeDashboardWidget*>(this), QStringLiteral("加成奖励说明"),
        QStringLiteral("「%1」包含加成奖励。\n来源：每日挑战加成池。\n会员要求：当前版本不需要会员。\n加成内容：完成后额外提升奖励展示或金币奖励。\n领取条件：任务完成后点击宝箱领取。")
            .arg(task.title));
}

void ChallengeDashboardWidget::routeTaskAction(const TaskItem& task)
{
    if (task.type.contains(QStringLiteral("树种"))) {
        emit openPlantSettingsRequested();
    } else if (task.type.contains(QStringLiteral("成长"))) {
        emit growthRulesRequested();
    } else {
        emit openFocusRequested();
    }
}

void ChallengeDashboardWidget::drawHeader(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    QLinearGradient header(rect.topLeft(), rect.topRight());
    header.setColorAt(0.0, QColor("#083C43"));
    header.setColorAt(0.52, QColor("#0B6970"));
    header.setColorAt(1.0, QColor("#78C6C4"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(header);
    painter.drawRoundedRect(rect, 28, 28);

    QRadialGradient glow(QPointF(rect.right() - 160, rect.top() + 30), 220);
    glow.setColorAt(0.0, QColor(255, 255, 210, 130));
    glow.setColorAt(0.55, QColor(109, 210, 205, 58));
    glow.setColorAt(1.0, QColor(109, 210, 205, 0));
    painter.setBrush(glow);
    painter.drawEllipse(QRectF(rect.right() - 330, rect.top() - 130, 360, 260));

    painter.setPen(QColor("#F7FFF7"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 25, QFont::Bold));
    painter.drawText(rect.adjusted(30, 22, -30, -82), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("挑战中心"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::DemiBold));
    painter.setPen(QColor(247, 255, 247, 190));
    painter.drawText(rect.adjusted(32, 70, -360, -42), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("完成每日任务，收集奖励，让森林持续成长"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    painter.drawText(rect.adjusted(32, 97, -360, -18), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("本地挑战数据不会跨设备同步"));

    const qreal pillW = 116;
    QRectF coin(rect.right() - pillW * 2 - 40, rect.top() + 24, pillW, 38);
    QRectF growth(rect.right() - pillW - 26, rect.top() + 24, pillW, 38);
    drawResourcePill(painter, coin, QStringLiteral("🪙"), QString::number(coins_), isHovered(HitRole::Coin));
    drawResourcePill(painter, growth, QStringLiteral("🌿"), QString::number(data.rewardGrowth), isHovered(HitRole::Growth));
    addHit(coin, HitRole::Coin, -1, QStringLiteral("查看金币详情或进入植物商城"));
    addHit(growth, HitRole::Growth, -1, QStringLiteral("查看成长值计算规则"));
    painter.restore();
}

void ChallengeDashboardWidget::drawMonthlyCard(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    painter.save();
    QLinearGradient card(rect.topLeft(), rect.bottomRight());
    card.setColorAt(0.0, QColor("#0B5660"));
    card.setColorAt(0.56, QColor("#10828A"));
    card.setColorAt(1.0, QColor("#9AD8CF"));
    painter.setPen(isHovered(HitRole::MonthlyCard) ? QPen(QColor("#E8FFF8"), 2) : Qt::NoPen);
    painter.setBrush(QColor(29, 78, 82, 38));
    painter.drawRoundedRect(rect.translated(0, 8), 30, 30);
    painter.setBrush(card);
    painter.drawRoundedRect(rect, 30, 30);
    addHit(rect, HitRole::MonthlyCard, -1, QStringLiteral("打开本月挑战详情"));

    const bool compact = rect.width() < 900;
    const qreal plantWidth = compact ? 160 : 240;
    const qreal plantRight = compact ? 28 : 60;
    QRectF plantRect(rect.right() - plantWidth - plantRight, rect.top() + 22, plantWidth, 162);
    const qreal textRight = rect.right() - plantRect.left() + 24;

    painter.setPen(QColor("#F7FFF7"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 25, QFont::Bold));
    painter.drawText(rect.adjusted(34, 22, -textRight, -138), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("本月挑战"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), compact ? 11 : 13, QFont::DemiBold));
    painter.setPen(QColor(247, 255, 247, 215));
    painter.drawText(QRectF(rect.left() + 36, rect.top() + 66,
                            plantRect.left() - rect.left() - 70, compact ? 44 : 34),
                     Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                     QStringLiteral("完成本月专注目标，即可获得森林植物奖励"));

    const qreal barWidth = compact
        ? std::max<qreal>(190, plantRect.left() - rect.left() - 72)
        : rect.width() * 0.46;
    QRectF bar(rect.left() + 36, rect.top() + (compact ? 118 : 128), barWidth, compact ? 18 : 24);
    drawProgressBar(painter, bar, data.monthMinutes, data.monthTarget, QColor("#56D0CE"));
    addHit(bar.adjusted(-4, -6, 4, 6), HitRole::MonthlyProgress, -1,
           QStringLiteral("当前 %1 / 目标 %2，完成 %3%")
               .arg(std::min(data.monthMinutes, data.monthTarget))
               .arg(data.monthTarget)
               .arg(completionPercent(data.monthMinutes, data.monthTarget)));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(bar.adjusted(0, 24, 0, 38), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("进度 %1 / %2 分钟").arg(std::min(data.monthMinutes, data.monthTarget)).arg(data.monthTarget));

    QRectF timeRect = compact
        ? QRectF(rect.left() + 180, rect.bottom() - 42, plantRect.left() - rect.left() - 206, 28)
        : QRectF(bar.right() + 24, bar.top() + 24, 190, 30);
    painter.setPen(QColor(isHovered(HitRole::MonthlyTime) ? "#FFFFFF" : "#DFF7F1"));
    painter.drawText(timeRect, Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("⏱ 剩余 %1 天").arg(data.remainingDays));
    addHit(timeRect.adjusted(-6, -4, 8, 4), HitRole::MonthlyTime, -1, QStringLiteral("查看本月挑战刷新规则"));

    QRectF claimRect = compact
        ? QRectF(rect.left() + 36, rect.bottom() - 44, 128, 28)
        : QRectF(bar.right() + 24, bar.top() - 4, 128, 30);
    const bool finished = data.monthMinutes >= data.monthTarget;
    drawButton(painter, claimRect, monthlyClaimed_ ? QStringLiteral("已领取") : QStringLiteral("领取奖励"),
               QColor("#F2F4C6"), finished && !monthlyClaimed_, isHovered(HitRole::MonthlyReward));
    addHit(claimRect, HitRole::MonthlyReward, -1,
           finished ? QStringLiteral("领取或查看本月挑战奖励状态")
                    : QStringLiteral("挑战完成后可领取奖励"));

    drawRewardPlant(painter, plantRect, data.rewardPlant);
    QRectF plantBadge(plantRect.center().x() - (compact ? 62 : 83), rect.bottom() - 58,
                      compact ? 124 : 166, 38);
    painter.setBrush(QColor(255, 255, 255, isHovered(HitRole::MonthlyPlant) ? 92 : 55));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(plantBadge, 19, 19);
    painter.setPen(QColor("#F7FFF7"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 13, QFont::Bold));
    painter.drawText(plantBadge, Qt::AlignCenter, data.rewardPlant.name);
    addHit(plantRect.united(plantBadge), HitRole::MonthlyPlant, -1, QStringLiteral("查看植物详情"));
    painter.restore();
}

void ChallengeDashboardWidget::drawCheckinCard(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    drawCard(painter, rect, 28);
    painter.save();
    painter.setPen(QColor("#253B36"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 21, QFont::Bold));
    painter.drawText(rect.adjusted(26, 20, -26, -136), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("每日签到"));

    QRectF refreshRect(rect.right() - 190, rect.top() + 26, 160, 28);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
    painter.setPen(QColor(isHovered(HitRole::CheckinRefresh) ? "#2B6C54" : "#7B8C86"));
    painter.drawText(refreshRect, Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("⏱ %1 小时后刷新").arg(24 - QTime::currentTime().hour()));
    addHit(refreshRect.adjusted(-8, -4, 8, 4), HitRole::CheckinRefresh, -1, QStringLiteral("查看每日签到刷新规则"));

    const qreal gap = 10;
    const qreal boxW = (rect.width() - 52 - gap * 4) / 5;
    for (int i = 0; i < data.checkins.size(); ++i) {
        QRectF box(rect.left() + 26 + i * (boxW + gap), rect.top() + 88, boxW, 82);
        const bool hovered = isHovered(HitRole::CheckinReward, i);
        QLinearGradient grad(box.topLeft(), box.bottomRight());
        grad.setColorAt(0.0, data.checkins[i].state == QStringLiteral("今日可领取") ? QColor("#FFF6B9") : QColor("#D8F0FF"));
        grad.setColorAt(1.0, QColor(255, 255, 255, hovered ? 245 : 185));
        painter.setBrush(grad);
        painter.setPen(QPen(QColor(255, 255, 255, hovered ? 230 : 150), hovered ? 2 : 1));
        painter.drawRoundedRect(box, 18, 18);
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 21, QFont::Bold));
        painter.setPen(QColor("#2F6F5A"));
        painter.drawText(box.adjusted(0, 6, 0, -32), Qt::AlignCenter, data.checkins[i].icon);
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
        painter.drawText(box.adjusted(0, 34, 0, -17), Qt::AlignCenter, data.checkins[i].text);
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8, QFont::DemiBold));
        painter.setPen(QColor("#6F8580"));
        painter.drawText(box.adjusted(2, 58, -2, -3), Qt::AlignCenter, data.checkins[i].state);
        addHit(box, HitRole::CheckinReward, i, QStringLiteral("点击查看或领取签到奖励"));
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawTaskPanel(QPainter& painter, const QRectF& rect, const DataModel& data) const
{
    drawCard(painter, rect, 28);
    painter.save();
    painter.setPen(QColor("#253B36"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 21, QFont::Bold));
    painter.drawText(rect.adjusted(26, 18, -26, -rect.height() + 58), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("每日挑战"));
    QRectF refreshRect(rect.right() - 190, rect.top() + 25, 160, 28);
    painter.setPen(QColor(isHovered(HitRole::CheckinRefresh, 99) ? "#2B6C54" : "#7B8C86"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
    painter.drawText(refreshRect, Qt::AlignRight | Qt::AlignVCenter,
                     QStringLiteral("⏱ 今日自动刷新"));
    addHit(refreshRect.adjusted(-8, -4, 8, 4), HitRole::CheckinRefresh, 99, QStringLiteral("查看每日挑战刷新规则"));

    const qreal gap = 14;
    const qreal cardH = (rect.height() - 90 - gap * 3) / 4;
    for (int i = 0; i < data.tasks.size(); ++i) {
        QRectF taskRect(rect.left() + 26, rect.top() + 74 + i * (cardH + gap), rect.width() - 52, cardH);
        drawTaskCard(painter, taskRect, data.tasks[i], i);
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawTaskCard(QPainter& painter, const QRectF& rect, const TaskItem& task, int index) const
{
    painter.save();
    QLinearGradient bg(rect.topLeft(), rect.bottomRight());
    bg.setColorAt(0.0, QColor(218, 242, 255, 224));
    bg.setColorAt(0.58, QColor(205, 236, 248, 218));
    bg.setColorAt(1.0, QColor(238, 250, 255, 218));
    painter.setPen(task.plus ? QPen(QColor("#5FD889"), 2) : QPen(QColor(255, 255, 255, isHovered(HitRole::TaskCard, index) ? 230 : 0), 2));
    painter.setBrush(bg);
    painter.drawRoundedRect(rect, 20, 20);
    addHit(rect, HitRole::TaskCard, index, QStringLiteral("打开任务详情"));

    painter.setPen(QPen(QColor(255, 255, 255, 72), 18));
    painter.drawLine(QPointF(rect.left() + rect.width() * 0.52, rect.top() + 10),
                     QPointF(rect.left() + rect.width() * 0.40, rect.bottom() - 10));
    painter.drawLine(QPointF(rect.left() + rect.width() * 0.76, rect.top() + 10),
                     QPointF(rect.left() + rect.width() * 0.64, rect.bottom() - 10));

    if (task.plus) {
        QRectF plus(rect.left(), rect.top() - 12, 126, 30);
        QLinearGradient plusGrad(plus.topLeft(), plus.topRight());
        plusGrad.setColorAt(0.0, QColor("#6FE56E"));
        plusGrad.setColorAt(1.0, QColor("#21B9C8"));
        painter.setPen(Qt::NoPen);
        painter.setBrush(plusGrad);
        painter.drawRoundedRect(plus, 15, 15);
        painter.setPen(QColor("#FFFFFF"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 11, QFont::Bold));
        painter.drawText(plus, Qt::AlignCenter, QStringLiteral("加成奖励"));
        addHit(plus, HitRole::TaskPlus, index, QStringLiteral("查看加成奖励说明"));
    }

    painter.setPen(QColor("#253B36"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 15, QFont::Bold));
    drawTextFit(painter, QRectF(rect.left() + 22, rect.top() + 16, rect.width() - 124, 22),
                task.title, Qt::AlignLeft | Qt::AlignVCenter, 10);
    painter.setPen(QColor("#6F8580"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::DemiBold));
    painter.drawText(QRectF(rect.left() + 22, rect.top() + 40, rect.width() - 124, 18),
                      Qt::AlignLeft | Qt::AlignVCenter, task.type);

    const qreal barTop = std::max(rect.top() + 60.0, rect.bottom() - 26.0);
    QRectF bar(rect.left() + 22, barTop, rect.width() - 120, 14);
    drawProgressBar(painter, bar, task.progress, task.target, QColor("#62CDA7"));
    addHit(bar.adjusted(-4, -6, 4, 6), HitRole::TaskProgress, index,
           QStringLiteral("当前 %1 / 目标 %2，完成 %3%")
               .arg(std::min(task.progress, task.target))
               .arg(task.target)
               .arg(completionPercent(task.progress, task.target)));
    painter.setPen(QColor("#71827E"));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
    painter.drawText(bar, Qt::AlignCenter,
                     QStringLiteral("%1 / %2").arg(std::min(task.progress, task.target)).arg(task.target));

    QRectF chestRect(rect.right() - 70, rect.center().y() - 24, 48, 48);
    drawChest(painter, chestRect, task.chestColor);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::Bold));
    painter.setPen(QColor("#2B6C54"));
    painter.drawText(chestRect, Qt::AlignCenter, task.rewardIcon);
    QRectF rewardAmount(chestRect.left() - 3, chestRect.bottom() - 2, chestRect.width() + 6, 16);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 8, QFont::Bold));
    painter.setPen(QColor("#2B6C54"));
    painter.drawText(rewardAmount, Qt::AlignCenter, QStringLiteral("x%1").arg(task.rewardCoins));
    if (task.progress >= task.target && !taskClaimed_.value(index, false)) {
        painter.setPen(QPen(QColor("#FFE36A"), 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(chestRect.adjusted(-7, -7, 7, 7));
    }
    addHit(chestRect.adjusted(-8, -8, 8, 8), HitRole::TaskChest, index, QStringLiteral("查看或领取任务奖励"));
    if (task.arrow) {
        QRectF arrowRect(rect.right() - 24, rect.center().y() - 16, 18, 32);
        painter.setPen(QColor(isHovered(HitRole::TaskArrow, index) ? "#0B6970" : "#2E554B"));
        painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 22, QFont::Bold));
        painter.drawText(arrowRect, Qt::AlignCenter, QString::fromUtf8(u8"›"));
        addHit(arrowRect.adjusted(-8, -10, 8, 10), HitRole::TaskArrow, index, QStringLiteral("跳转到对应功能"));
    }
    painter.restore();
}

void ChallengeDashboardWidget::drawResourcePill(QPainter& painter, const QRectF& rect,
                                                const QString& icon, const QString& value,
                                                bool hovered) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 255, 255, hovered ? 112 : 72));
    painter.drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 14, QFont::Bold));
    painter.setPen(QColor("#F7FFF7"));
    painter.drawText(QRectF(rect.left() + 14, rect.top(), 30, rect.height()), Qt::AlignCenter, icon);
    painter.drawText(QRectF(rect.left() + 44, rect.top(), rect.width() - 54, rect.height()),
                     Qt::AlignLeft | Qt::AlignVCenter, value);
    painter.restore();
}

void ChallengeDashboardWidget::drawRewardPlant(QPainter& painter, const QRectF& rect, const PlantInfo& plant) const
{
    painter.save();
    painter.setBrush(QColor(255, 255, 255, isHovered(HitRole::MonthlyPlant) ? 52 : 32));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect.adjusted(16, 6, -16, -6), 24, 24);
    QPixmap pix = PlantImageUtils::loadPlantIcon(plant.iconPath);
    if (!pix.isNull()) {
        const QSize target = pix.size().scaled(QSize(static_cast<int>(rect.width() * 0.72), static_cast<int>(rect.height() * 0.88)),
                                               Qt::KeepAspectRatio);
        QRect drawRect(QPoint(static_cast<int>(rect.center().x() - target.width() / 2),
                              static_cast<int>(rect.center().y() - target.height() / 2 - 2)), target);
        painter.drawPixmap(drawRect, pix);
    }
    painter.setBrush(QColor(29, 78, 82, 45));
    painter.drawEllipse(QRectF(rect.center().x() - 76, rect.bottom() - 28, 152, 26));
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
    filled.setWidth(std::max<qreal>(10, filled.width() * ratio));
    painter.setBrush(fill);
    painter.drawRoundedRect(filled, filled.height() / 2, filled.height() / 2);
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
    painter.setPen(enabled ? QColor("#2B6C54") : QColor(247, 255, 247, 145));
    painter.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10, QFont::Bold));
    painter.drawText(rect, Qt::AlignCenter, text);
    painter.restore();
}

void ChallengeDashboardWidget::drawCard(QPainter& painter, const QRectF& rect, qreal radius) const
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(46, 87, 76, 25));
    painter.drawRoundedRect(rect.translated(0, 7), radius, radius);
    painter.setBrush(QColor(255, 255, 255, 238));
    painter.drawRoundedRect(rect, radius, radius);
    painter.restore();
}

void ChallengeDashboardWidget::drawTextFit(QPainter& painter, const QRectF& rect, const QString& text,
                                           int flags, int minPointSize) const
{
    QFont font = painter.font();
    while (font.pointSize() > minPointSize) {
        QFontMetrics fm(font);
        if (fm.horizontalAdvance(text) <= rect.width()) break;
        font.setPointSize(font.pointSize() - 1);
    }
    painter.save();
    painter.setFont(font);
    painter.drawText(rect, flags, text);
    painter.restore();
}
