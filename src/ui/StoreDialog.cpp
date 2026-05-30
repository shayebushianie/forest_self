#include "ui/StoreDialog.h"
#include "core/CoinManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>
#include <QPixmap>

StoreDialog::StoreDialog(CoinManager& coinManager, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("植物商城"));
    setFixedSize(450, 520);
    setObjectName("StoreDialog");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    // 金币余额展示栏
    auto* coinLabel = new QLabel(this);
    coinLabel->setText(QStringLiteral("<div style='text-align:center;'>"
        "<span style='color:#8A9A86;'>当前金币</span><br>"
        "<span style='color:#D8B257; font-size:28px; font-weight:bold;'>%1</span>"
        "</div>").arg(coinManager.balance()));
    coinLabel->setStyleSheet("background:transparent; border:none;");
    mainLayout->addWidget(coinLabel);

    QObject::connect(&coinManager, &CoinManager::sig_balanceChanged,
        this, [coinLabel](uint32_t bal) {
            coinLabel->setText(QStringLiteral("<div style='text-align:center;'>"
                "<span style='color:#8A9A86;'>当前金币</span><br>"
                "<span style='color:#D8B257; font-size:28px; font-weight:bold;'>%1</span>"
                "</div>").arg(bal));
        });

    // 滚动区域
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);

    auto* scrollContent = new QWidget(scrollArea);
    auto* listLayout = new QVBoxLayout(scrollContent);
    listLayout->setSpacing(10);
    listLayout->setContentsMargins(0, 0, 0, 0);

    struct PlantItem { const char* name; uint32_t type; uint32_t cost; const char* icon; };
    PlantItem items[] = {
        {"松树 (PineTree)",   1,  500, ":/images/pine.png"},
        {"玫瑰 (Rose)",       2,  500, ":/images/rose.png"},
        {"银杏树",            3, 1000, ":/images/oak.png"},
        {"向日葵",            4, 1000, ":/images/oak.png"},
        {"仙人掌",            5, 1500, ":/images/oak.png"},
    };

    for (const auto& item : items) {
        auto* card = new QWidget(scrollContent);
        card->setStyleSheet(
            "QWidget { background-color: #24332b; border: 1px solid #2e3f34; border-radius: 8px; }"
            "QLabel { background: transparent; border: none; }");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 8, 12, 8);

        auto* imgLabel = new QLabel(card);
        QPixmap pix(item.icon);
        imgLabel->setPixmap(pix.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        imgLabel->setStyleSheet("background:transparent; border:none;");
        cardLayout->addWidget(imgLabel);

        auto* nameLabel = new QLabel(QString::fromUtf8(item.name), card);
        nameLabel->setStyleSheet("font-weight:bold; font-size:15px; color:#E8EAE6; background:transparent; border:none;");
        cardLayout->addWidget(nameLabel);
        cardLayout->addStretch();

        auto* buyBtn = new QPushButton(card);
        buyBtn->setText(QStringLiteral("%1 金币").arg(item.cost));
        buyBtn->setObjectName("btnPrimary");
        QObject::connect(buyBtn, &QPushButton::clicked,
            this, [this, &coinManager, cost = item.cost]() {
                if (coinManager.spend(cost)) {
                    QMessageBox::information(this,
                        QStringLiteral("解锁成功"),
                        QStringLiteral("新植物已解锁！"));
                } else {
                    QMessageBox::warning(this,
                        QStringLiteral("金币不足"),
                        QStringLiteral("请先完成专注任务赚取金币。"));
                }
            });
        cardLayout->addWidget(buyBtn);

        listLayout->addWidget(card);
    }

    listLayout->addStretch();
    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeBtn);
}
