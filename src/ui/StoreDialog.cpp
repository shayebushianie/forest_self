#include "ui/StoreDialog.h"
#include "config/PlantCatalog.h"
#include "core/CoinManager.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QString coinText(uint32_t balance)
{
    return QStringLiteral("<div style='text-align:center;'>"
                          "<span style='color:#8A6D1F; font-size:28px; font-weight:bold;'>&#x1FA99; %1</span>"
                          "</div>").arg(balance);
}

}

StoreDialog::StoreDialog(CoinManager& coinManager, QWidget* parent)
    : QDialog(parent), coinManager_(coinManager)
{
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("植物商城"));
    setFixedSize(450, 520);
    setObjectName("StoreDialog");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    auto* coinLabel = new QLabel(coinText(coinManager_.balance()), this);
    coinLabel->setStyleSheet("background:transparent; border:none;");
    mainLayout->addWidget(coinLabel);

    QObject::connect(&coinManager_, &CoinManager::sig_balanceChanged,
                     this, [coinLabel](uint32_t balance) { coinLabel->setText(coinText(balance)); });

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollContent_ = new QWidget(scrollArea);
    listLayout_ = new QVBoxLayout(scrollContent_);
    listLayout_->setSpacing(10);
    listLayout_->setContentsMargins(0, 0, 0, 0);
    listLayout_->addStretch();
    scrollArea->setWidget(scrollContent_);
    mainLayout->addWidget(scrollArea, 1);

    rebuildItems();

    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    QObject::connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    mainLayout->addWidget(closeBtn);
}

void StoreDialog::rebuildItems()
{
    while (listLayout_->count() > 1) {
        QLayoutItem* item = listLayout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (const PlantDefinition& plant : PlantCatalog::all()) {
        auto* card = new QFrame(scrollContent_);
        card->setObjectName("storeCard");
        card->setAttribute(Qt::WA_Hover);
        card->setStyleSheet(
            "QFrame#storeCard { background-color: #24332b; border: 1px solid #2e3f34; border-radius: 8px; }"
            "QFrame#storeCard:hover { background-color: #315747; border: 1px solid #8A9A52; }"
            "QFrame#storeCard QLabel { background: transparent; border: none; }"
            "QLabel#plantImageSlot { background-color:#17231c; border:1px solid #4F684E; border-radius:8px; }");
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(12, 10, 12, 10);
        cardLayout->setSpacing(12);

        auto* imageLabel = new QLabel(card);
        imageLabel->setObjectName("plantImageSlot");
        imageLabel->setFixedSize(58, 58);
        imageLabel->setAlignment(Qt::AlignCenter);
        const QPixmap pixmap = PlantImageUtils::loadPlantIcon(plant.iconPath);
        if (!pixmap.isNull()) {
            imageLabel->setPixmap(pixmap.scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        cardLayout->addWidget(imageLabel);

        auto* nameLabel = new QLabel(
            QStringLiteral("%1 (%2)").arg(plant.displayName, plant.internalName), card);
        nameLabel->setStyleSheet("font-weight:bold; font-size:15px; color:#E8EAE6; background:transparent; border:none;");
        cardLayout->addWidget(nameLabel);
        cardLayout->addStretch();

        auto* buyButton = new QPushButton(card);
        buyButton->setObjectName("btnPrimary");
        if (coinManager_.isPlantUnlocked(plant.type)) {
            buyButton->setText(QStringLiteral("已解锁"));
            buyButton->setEnabled(false);
        } else {
            buyButton->setText(QStringLiteral("&#x1FA99; %1").arg(plant.storeCost));
            QObject::connect(buyButton, &QPushButton::clicked, this,
                             [this, cost = plant.storeCost, type = plant.type]() {
                if (coinManager_.spend(cost)) {
                    coinManager_.unlockPlant(type);
                    DialogPresenter::information(this, QStringLiteral("解锁成功"),
                                             QStringLiteral("新植物已解锁。"));
                    rebuildItems();
                } else {
                    DialogPresenter::warning(this, QStringLiteral("余额不足"),
                                         QStringLiteral("请先完成专注任务赚取金币。"));
                }
            });
        }
        cardLayout->addWidget(buyButton);
        listLayout_->insertWidget(listLayout_->count() - 1, card);
    }
}
