#include "ui/GuardianDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QFrame>

GuardianDialog::GuardianDialog(GuardianManager* mgr, QWidget* parent)
    : QDialog(parent), m_mgr(mgr) {
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("⏰ 时间守护"));
    setFixedSize(480, 420);
    setStyleSheet(
        "QDialog { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        " stop:0 #3E8D75, stop:1 #357A64); border:2px solid #B6E37A; border-radius:22px; }");

    auto* main = new QVBoxLayout(this);
    main->setSpacing(12);
    main->setContentsMargins(24, 20, 24, 20);

    // Title
    auto* title = new QLabel(QStringLiteral("⏰ 时间守护"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:22px; font-weight:bold; color:#F7FFF7; background:transparent; border:none;");
    main->addWidget(title);

    // Progress card
    auto* progCard = new QFrame;
    progCard->setStyleSheet(
        "QFrame { background:rgba(255,255,255,28); border-radius:12px; }");
    auto* progLayout = new QVBoxLayout(progCard);
    progLayout->setContentsMargins(24, 18, 24, 18);
    progLayout->setSpacing(8);

    m_progressNum = new QLabel;
    m_progressNum->setAlignment(Qt::AlignCenter);
    m_progressNum->setFixedHeight(44);
    m_progressNum->setStyleSheet("font-size:34px; font-weight:bold; color:#F2F4C6; background:transparent;");
    progLayout->addWidget(m_progressNum);

    m_progress = new QProgressBar;
    m_progress->setFixedHeight(20);
    m_progress->setTextVisible(false);
    m_progress->setStyleSheet(
        "QProgressBar { background:rgba(43,108,84,120); border-radius:9px; }"
        "QProgressBar::chunk { background:#F2F4C6; border-radius:9px; }");
    progLayout->addWidget(m_progress);

    m_progressPct = new QLabel;
    m_progressPct->setAlignment(Qt::AlignCenter);
    m_progressPct->setFixedHeight(18);
    m_progressPct->setStyleSheet("font-size:13px; color:rgba(247,255,247,170); background:transparent;");
    progLayout->addWidget(m_progressPct);

    main->addWidget(progCard);

    // Three stat cards
    auto* statsRow = new QHBoxLayout;
    statsRow->setSpacing(10);

    auto makeCard = [](const QString& icon, QLabel*& valLabel) -> QFrame* {
        auto* card = new QFrame;
        card->setFixedHeight(100);
        card->setStyleSheet(
            "QFrame { background:rgba(255,255,255,28); border-radius:12px; }");
        auto* l = new QVBoxLayout(card);
        l->setContentsMargins(8, 10, 8, 10);
        l->setSpacing(4);
        auto* iconLbl = new QLabel(icon);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setStyleSheet("font-size:20px; background:transparent;");
        l->addWidget(iconLbl);
        valLabel = new QLabel;
        valLabel->setAlignment(Qt::AlignCenter);
        valLabel->setFixedHeight(34);
        valLabel->setStyleSheet("font-size:26px; font-weight:bold; color:#F2F4C6; background:transparent;");
        l->addWidget(valLabel);
        auto* hint = new QLabel;
        hint->setAlignment(Qt::AlignCenter);
        hint->setStyleSheet("font-size:11px; color:rgba(247,255,247,160); background:transparent;");
        hint->setText(QStringLiteral("天"));
        l->addWidget(hint);
        return card;
    };

    auto* streakCard = makeCard(QStringLiteral("🔥"), m_curStreakVal);
    auto* longestCard = makeCard(QStringLiteral("🏆"), m_longestStreakVal);
    auto* totalCard = makeCard(QStringLiteral("⏱"), m_totalVal);

    statsRow->addWidget(streakCard);
    statsRow->addWidget(longestCard);
    statsRow->addWidget(totalCard);
    main->addLayout(statsRow);

    // Goal settings
    auto* goalRow = new QHBoxLayout;
    goalRow->setSpacing(8);
    auto* goalLabel = new QLabel(QStringLiteral("🎯 每日目标"));
    goalLabel->setStyleSheet("font-size:15px; font-weight:bold; color:#F7FFF7; background:transparent;");
    goalRow->addWidget(goalLabel);
    goalRow->addStretch();
    m_goalSpin = new QSpinBox;
    m_goalSpin->setRange(10, 600);
    m_goalSpin->setValue(static_cast<int>(m_mgr->dailyGoalMinutes()));
    m_goalSpin->setSuffix(QStringLiteral(" 分钟"));
    m_goalSpin->setFixedWidth(130);
    m_goalSpin->setStyleSheet(
        "QSpinBox { background:white; border:none; border-radius:6px;"
        " padding:6px 10px; font-weight:bold; font-size:14px; }"
        "QSpinBox::up-button { width:24px; }"
        "QSpinBox::down-button { width:24px; }");
    QObject::connect(m_goalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_mgr->setDailyGoalMinutes(static_cast<uint32_t>(v));
        m_mgr->save();
        refresh();
    });
    goalRow->addWidget(m_goalSpin);
    main->addLayout(goalRow);

    main->addStretch();

    // Close button
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"));
    closeBtn->setStyleSheet(
        "QPushButton { background:rgba(255,255,255,30); color:#F7FFF7; border:1px solid rgba(255,255,255,50);"
        " border-radius:8px; padding:8px 28px; font-weight:bold; font-size:14px; }"
        "QPushButton:hover { background:rgba(255,255,255,50); }"
        "QPushButton:pressed { background:rgba(255,255,255,20); }");
    main->addWidget(closeBtn, 0, Qt::AlignCenter);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    refresh();
}

void GuardianDialog::refresh() {
    m_mgr->checkDayBoundary();
    uint32_t goal = m_mgr->dailyGoalMinutes();
    uint32_t today = m_mgr->todayMinutes();
    uint32_t pct = goal > 0 ? qMin<uint32_t>(100, today * 100 / goal) : 0;

    m_progressNum->setText(QStringLiteral("%1 / %2 分钟").arg(today).arg(goal));
    m_progress->setMaximum(goal > 0 ? static_cast<int>(goal) : 100);
    m_progress->setValue(static_cast<int>(qMin(goal, today)));
    m_progressPct->setText(QStringLiteral("今日完成 %1%").arg(pct));

    m_curStreakVal->setText(QStringLiteral("%1").arg(m_mgr->currentStreak()));
    m_longestStreakVal->setText(QStringLiteral("%1").arg(m_mgr->longestStreak()));
    m_totalVal->setText(QStringLiteral("%1").arg(m_mgr->totalMinutes() / 60));
}
