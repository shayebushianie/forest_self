#include "AchievementDialog.h"
#include "core/AchievementEngine.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QLabel>
#include <QPushButton>

AchievementDialog::AchievementDialog(AchievementEngine* mgr, QWidget* parent)
    : QDialog(parent)
    , m_mgr(mgr) {

    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("🏆 成就"));
    resize(480, 500);
    setupUI();
}

void AchievementDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    QLabel* title = new QLabel(
        QStringLiteral("<h2>🏆 成就</h2>"
                       "<p style='color:#666;'>完成各种挑战解锁成就</p>"),
        this);
    title->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(title);

    int total = m_mgr->count();
    int unlocked = 0;
    for (int i = 0; i < total; ++i) {
        if (m_mgr->isUnlocked(i)) ++unlocked;
    }
    QLabel* stats = new QLabel(
        QStringLiteral("已解锁: %1 / %2").arg(unlocked).arg(total), this);
    stats->setAlignment(Qt::AlignCenter);
    QFont sf = stats->font();
    sf.setPointSize(12);
    sf.setBold(true);
    stats->setFont(sf);
    stats->setStyleSheet(QStringLiteral("color: #2d6a2d; padding: 4px;"));
    mainLayout->addWidget(stats);

    QFrame* progressBar = new QFrame(this);
    progressBar->setFixedHeight(14);
    progressBar->setStyleSheet(QStringLiteral(
        "QFrame { background: #e0f0e0; border-radius: 7px; }"));
    QFrame* progressFill = new QFrame(progressBar);
    double pct = total > 0 ? (double)unlocked / total : 0.0;
    int fillW = static_cast<int>(448 * pct);
    if (fillW < 1 && unlocked > 0) fillW = 1;
    progressFill->setFixedSize(fillW, 14);
    progressFill->setStyleSheet(QStringLiteral(
        "QFrame { background: #ffd700; border-radius: 7px; }"));
    mainLayout->addWidget(progressBar);

    QScrollArea* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; }"));

    QWidget* container = new QWidget();
    QVBoxLayout* cardLayout = new QVBoxLayout(container);
    cardLayout->setSpacing(8);

    for (int i = 0; i < total; ++i) {
        const auto& a = m_mgr->all()[i];
        bool isUnlocked = m_mgr->isUnlocked(i);

        QFrame* card = new QFrame(container);
        card->setFrameShape(QFrame::StyledPanel);

        QString borderColor;
        if (isUnlocked) {
            if (a.rarity == QStringLiteral("gold"))
                borderColor = QStringLiteral("#ffd700");
            else if (a.rarity == QStringLiteral("silver"))
                borderColor = QStringLiteral("#c0c0c0");
            else
                borderColor = QStringLiteral("#a0c0a0");
        } else {
            borderColor = QStringLiteral("#e0e0e0");
        }

        QString bgColor = isUnlocked ? QStringLiteral("white") : QStringLiteral("#f5f5f5");
        card->setStyleSheet(QStringLiteral(
            "QFrame { background: %1; border: 2px solid %2;"
            " border-radius: 8px; padding: 8px; }").arg(bgColor, borderColor));

        QHBoxLayout* row = new QHBoxLayout(card);
        row->setSpacing(14);

        QLabel* iconLabel = new QLabel(a.icon, card);
        QFont iconFont = iconLabel->font();
        iconFont.setPointSize(26);
        iconLabel->setFont(iconFont);
        if (!isUnlocked) {
            iconLabel->setStyleSheet(QStringLiteral("color: #ddd;"));
        }
        row->addWidget(iconLabel);

        QVBoxLayout* infoCol = new QVBoxLayout();
        QString name, desc;
        if (isUnlocked) {
            name = a.name;
            desc = a.description;
        } else {
            name = QStringLiteral("???");
            desc = QStringLiteral("???");
        }

        QLabel* nameLabel = new QLabel(name, card);
        QFont nf = nameLabel->font();
        nf.setBold(true);
        nf.setPointSize(11);
        nameLabel->setFont(nf);
        QColor nameColor = isUnlocked ? QColor(33, 33, 33) : QColor(200, 200, 200);
        nameLabel->setStyleSheet(QStringLiteral("color: %1;").arg(nameColor.name()));
        infoCol->addWidget(nameLabel);

        QLabel* descLabel = new QLabel(desc, card);
        descLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 10px;")
            .arg(isUnlocked ? "#666" : "#ddd"));
        infoCol->addWidget(descLabel);

        QLabel* rewardLabel = new QLabel(QStringLiteral("奖励：🪙 %1").arg(a.rewardCoins), card);
        rewardLabel->setStyleSheet(QStringLiteral(
            "color: %1; font-size: 10px; font-weight: bold;")
            .arg(isUnlocked ? "#b8860b" : "#c7c7c7"));
        infoCol->addWidget(rewardLabel);

        row->addLayout(infoCol, 1);

        QString rarityLabel;
        if (a.rarity == QStringLiteral("gold"))
            rarityLabel = QStringLiteral("🌟 传说");
        else if (a.rarity == QStringLiteral("silver"))
            rarityLabel = QStringLiteral("✨ 稀有");
        else
            rarityLabel = QStringLiteral("普通");

        QLabel* rarityBadge = new QLabel(isUnlocked ? rarityLabel : QString(), card);
        if (isUnlocked) {
            QString badgeColor = a.rarity == QStringLiteral("gold")
                ? QStringLiteral("#b8860b")
                : (a.rarity == QStringLiteral("silver")
                    ? QStringLiteral("#708090") : QStringLiteral("#888"));
            rarityBadge->setStyleSheet(QStringLiteral(
                "color: %1; font-size: 9px; font-weight: bold;").arg(badgeColor));
        }
        row->addWidget(rarityBadge);

        cardLayout->addWidget(card);
    }

    cardLayout->addStretch();
    scroll->setWidget(container);
    mainLayout->addWidget(scroll, 1);

    QPushButton* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    closeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #4caf50; color: white;"
        " border: none; border-radius: 6px; padding: 8px 24px;"
        " font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background: #45a049; }"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    mainLayout->addLayout(btnRow);
}
