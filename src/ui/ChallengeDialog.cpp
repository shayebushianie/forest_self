#include "ui/ChallengeDialog.h"
#include "ui/DialogPresenter.h"
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QTabWidget>
#include <QDate>

ChallengeDialog::ChallengeDialog(ChallengeManager* mgr,
                                 const QVector<uint32_t>& friendIds,
                                 QWidget* parent)
    : QDialog(parent), mgr_(mgr) {
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("🏆 专注挑战"));
    setMinimumSize(650, 500);

    auto* mainLayout = new QVBoxLayout(this);

    // --- create challenge ---
    auto* createGroup = new QGroupBox(QStringLiteral("创建新挑战"));
    auto* form = new QFormLayout(createGroup);

    friendCombo_ = new QComboBox;
    for (auto fid : friendIds)
        friendCombo_->addItem(QStringLiteral("用户 %1").arg(fid), fid);
    form->addRow(QStringLiteral("选择好友:"), friendCombo_);

    minutesSpin_ = new QSpinBox;
    minutesSpin_->setRange(10, 99999);
    minutesSpin_->setValue(300);
    minutesSpin_->setSuffix(QStringLiteral(" 分钟"));
    form->addRow(QStringLiteral("目标专注时长:"), minutesSpin_);

    endDateEdit_ = new QDateEdit(QDate::currentDate().addDays(7));
    endDateEdit_->setMinimumDate(QDate::currentDate().addDays(1));
    endDateEdit_->setCalendarPopup(true);
    form->addRow(QStringLiteral("截止日期:"), endDateEdit_);

    auto* btnRow = new QHBoxLayout;
    createBtn_ = new QPushButton(QStringLiteral("发送挑战"));
    createBtn_->setStyleSheet(
        "QPushButton{background:#7EC8A0;color:white;padding:6px 20px;"
        "border-radius:6px;font-weight:bold;}");
    btnRow->addStretch();
    btnRow->addWidget(createBtn_);
    form->addRow(btnRow);

    mainLayout->addWidget(createGroup);

    // --- tabs for active / history ---
    auto* tabs = new QTabWidget;

    activeTable_ = new QTableWidget;
    activeTable_->setColumnCount(5);
    activeTable_->setHorizontalHeaderLabels({
        QStringLiteral("对手"), QStringLiteral("目标"),
        QStringLiteral("截止"), QStringLiteral("我方进度"),
        QStringLiteral("操作")});
    activeTable_->horizontalHeader()->setStretchLastSection(true);
    activeTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    activeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(activeTable_, QStringLiteral("进行中"));

    historyTable_ = new QTableWidget;
    historyTable_->setColumnCount(4);
    historyTable_->setHorizontalHeaderLabels({
        QStringLiteral("对手"), QStringLiteral("目标"),
        QStringLiteral("结果"), QStringLiteral("最终进度")});
    historyTable_->horizontalHeader()->setStretchLastSection(true);
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tabs->addTab(historyTable_, QStringLiteral("历史"));

    mainLayout->addWidget(tabs);

    connect(createBtn_, &QPushButton::clicked,
            this, &ChallengeDialog::onCreateClicked);

    refreshActive();
    refreshHistory();
}

void ChallengeDialog::refreshActive() {
    auto challenges = mgr_->active();
    activeTable_->setRowCount(challenges.size());
    for (int i = 0; i < challenges.size(); ++i) {
        auto& c = challenges[i];
        uint32_t opponent = (c.creatorId == mgr_->localUserId())
                                ? c.targetId : c.creatorId;
        activeTable_->setItem(i, 0, new QTableWidgetItem(
            QStringLiteral("用户 %1").arg(opponent)));
        activeTable_->setItem(i, 1, new QTableWidgetItem(
            QStringLiteral("%1 分钟").arg(c.targetMinutes)));
        activeTable_->setItem(i, 2, new QTableWidgetItem(
            c.endDate.toString(Qt::ISODate)));

        uint32_t myProgress = (c.creatorId == mgr_->localUserId())
                                ? c.creatorProgress : c.targetProgress;
        activeTable_->setItem(i, 3, new QTableWidgetItem(
            QStringLiteral("%1 / %2 分钟")
                .arg(myProgress).arg(c.targetMinutes)));

        if (c.status == ChallengeStatus::PENDING
            && c.targetId == mgr_->localUserId()) {
            auto* acceptBtn = new QPushButton(QStringLiteral("接受"));
            auto* rejectBtn = new QPushButton(QStringLiteral("拒绝"));
            auto* w = new QWidget;
            auto* hl = new QHBoxLayout(w);
            hl->setContentsMargins(2, 2, 2, 2);
            hl->addWidget(acceptBtn);
            hl->addWidget(rejectBtn);
            activeTable_->setCellWidget(i, 4, w);
            connect(acceptBtn, &QPushButton::clicked, this, [this, id = c.id]() {
                mgr_->acceptChallenge(id);
                mgr_->save();
                refreshActive();
                refreshHistory();
            });
            connect(rejectBtn, &QPushButton::clicked, this, [this, id = c.id]() {
                mgr_->rejectChallenge(id);
                mgr_->save();
                refreshActive();
                refreshHistory();
            });
        } else {
            activeTable_->setItem(i, 4, new QTableWidgetItem(
                c.status == ChallengeStatus::PENDING
                    ? QStringLiteral("等待对方接受")
                    : QStringLiteral("进行中")));
        }
    }
}

void ChallengeDialog::refreshHistory() {
    auto challenges = mgr_->history();
    historyTable_->setRowCount(challenges.size());
    for (int i = 0; i < challenges.size(); ++i) {
        auto& c = challenges[i];
        uint32_t opponent = (c.creatorId == mgr_->localUserId())
                                ? c.targetId : c.creatorId;
        historyTable_->setItem(i, 0, new QTableWidgetItem(
            QStringLiteral("用户 %1").arg(opponent)));
        historyTable_->setItem(i, 1, new QTableWidgetItem(
            QStringLiteral("%1 分钟").arg(c.targetMinutes)));

        QString resultStr;
        if (c.status == ChallengeStatus::COMPLETED)
            resultStr = QStringLiteral("✅ 完成");
        else if (c.status == ChallengeStatus::REJECTED)
            resultStr = QStringLiteral("❌ 已拒绝");
        else
            resultStr = QStringLiteral("⏰ 已过期");
        historyTable_->setItem(i, 2, new QTableWidgetItem(resultStr));

        mgr_->recalculateProgress(const_cast<Challenge&>(c));
        uint32_t myProgress = (c.creatorId == mgr_->localUserId())
                                ? c.creatorProgress : c.targetProgress;
        historyTable_->setItem(i, 3, new QTableWidgetItem(
            QStringLiteral("%1 / %2 分钟")
                .arg(myProgress).arg(c.targetMinutes)));
    }
}

void ChallengeDialog::onCreateClicked() {
    if (friendCombo_->count() == 0) {
        DialogPresenter::empty(this, QStringLiteral("提示"),
                                 QStringLiteral("暂无好友"));
        return;
    }
    uint32_t fid = friendCombo_->currentData().toUInt();
    uint32_t mins = static_cast<uint32_t>(minutesSpin_->value());
    QDate end = endDateEdit_->date();
    mgr_->createChallenge(fid, mins, end);
    mgr_->save();
    DialogPresenter::information(this, QStringLiteral("成功"),
                             QStringLiteral("挑战已发送！"));
    refreshActive();
    refreshHistory();
}
