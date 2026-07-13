#include "ui/CommercePages.h"

#include "config/PlantCatalog.h"
#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/FocusResultService.h"
#include "core/GachaManager.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"

#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace {

QPixmap variantPixmap(const GachaManager::VariantDef& variant, const QSize& targetSize)
{
    QPixmap base = PlantImageUtils::loadPlantIcon(PlantCatalog::byType(variant.basePlantType).iconPath);
    if (base.isNull()) return {};

    QImage image = base.toImage().convertToFormat(QImage::Format_ARGB32);
    QColor tint(variant.tintColor);
    if (!tint.isValid()) tint = QColor("#7BC7A7");
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha == 0) continue;
            const int luminance = qBound(0, (qRed(line[x]) * 30 + qGreen(line[x]) * 59 + qBlue(line[x]) * 11) / 100, 255);
            const qreal shade = 0.55 + (luminance / 255.0) * 0.55;
            line[x] = qRgba(qBound(0, static_cast<int>(tint.red() * shade), 255),
                             qBound(0, static_cast<int>(tint.green() * shade), 255),
                             qBound(0, static_cast<int>(tint.blue() * shade), 255), alpha);
        }
    }
    return QPixmap::fromImage(image).scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void clearLayout(QVBoxLayout* layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

} // namespace

ShopPageController::ShopPageController(CoinManager& coins, FocusResultService& focusResults, QWidget* parent)
    : QObject(parent), coins_(coins), focusResults_(focusResults)
{
    page_ = new QWidget(parent);
    page_->setObjectName("shopPage");
    page_->setStyleSheet(
        "QWidget#shopPage { background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        " stop:0 #58AD8F, stop:0.7 #52A587, stop:1 #6FB79A); }");
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(20, 20, 20, 20);
    auto* title = new QLabel(QStringLiteral("植物商城"), page_);
    title->setStyleSheet("font-size:18px; font-weight:bold; color:#F7FFF7; background:transparent; border:none;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* scroll = new QScrollArea(page_);
    scroll->setWidgetResizable(true);
    auto* content = new QWidget(scroll);
    itemsLayout_ = new QVBoxLayout(content);
    itemsLayout_->setSpacing(10);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    refresh();
}

void ShopPageController::refresh()
{
    clearLayout(itemsLayout_);
    auto* parentWidget = itemsLayout_->parentWidget();
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        auto* card = new QFrame(parentWidget);
        card->setObjectName("shopCard");
        card->setAttribute(Qt::WA_Hover);
        card->setMinimumHeight(84);
        card->setStyleSheet(
            "QFrame#shopCard { background-color:rgba(255,255,255,42); border:1px solid rgba(255,255,255,88); border-radius:8px; }"
            "QFrame#shopCard:hover { background-color:rgba(255,255,255,76); border:1px solid #F2F4C6; }"
            "QFrame#shopCard QLabel { background:transparent; border:none; }"
            "QLabel#plantImageSlot { background:transparent; border:none; }");
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(12);
        auto* image = new QLabel(card);
        image->setObjectName("plantImageSlot");
        image->setFixedSize(64, 64);
        image->setAlignment(Qt::AlignCenter);
        const QPixmap pixmap = PlantImageUtils::loadPlantIcon(plant.iconPath);
        if (!pixmap.isNull()) image->setPixmap(pixmap.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        row->addWidget(image);
        auto* name = new QLabel(plant.displayName, card);
        name->setStyleSheet("font-weight:bold; font-size:15px; color:#F7FFF7; background:transparent; border:none;");
        row->addWidget(name);
        row->addStretch();
        auto* buy = new QPushButton(card);
        buy->setObjectName("btnPrimary");
        buy->setProperty("testId", QStringLiteral("shopPurchase_%1").arg(plant.type));
        buy->setCursor(Qt::PointingHandCursor);
        if (coins_.isPlantUnlocked(plant.type)) {
            buy->setText(QStringLiteral("已拥有"));
            buy->setEnabled(false);
            buy->setToolTip(QStringLiteral("该植物已解锁，可在专注主页中选择。"));
        } else {
            buy->setText(QStringLiteral("🪙 %1").arg(plant.storeCost));
            buy->setToolTip(QStringLiteral("使用金币解锁 %1").arg(plant.displayName));
            connect(buy, &QPushButton::clicked, this, [this, cost = plant.storeCost, type = plant.type]() {
                if (!coins_.spend(cost)) {
                    DialogPresenter::warning(page_, QStringLiteral("余额不足"), QStringLiteral("请先完成专注任务赚取 🪙。"));
                    return;
                }
                if (!coins_.unlockPlant(type)) {
                    coins_.refund(cost);
                    DialogPresenter::warning(page_, QStringLiteral("解锁失败"), QStringLiteral("植物解锁未能保存，已退还金币。"));
                    return;
                }
                DialogPresenter::information(page_, QStringLiteral("解锁成功"), QStringLiteral("新植物已解锁！"));
                refresh();
                focusResults_.evaluateAchievements();
                emit plantCatalogChanged();
                emit achievementsChanged();
            });
        }
        row->addWidget(buy);
        itemsLayout_->addWidget(card);
    }
    itemsLayout_->addStretch();
}

AchievementsPageController::AchievementsPageController(AchievementEngine& achievements, QWidget* parent)
    : QObject(parent), achievements_(achievements)
{
    page_ = new QWidget(parent);
    page_->setObjectName("achievementsPage");
    page_->setStyleSheet(
        "QWidget#achievementsPage { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #58AD8F, stop:0.65 #4FA286, stop:1 #4A907B); }"
        "QFrame[ach=\"true\"] { background-color:rgba(255,255,255,42); border:1px solid rgba(255,255,255,78); border-radius:10px; }"
        "QFrame[ach=\"true\"][unlocked=\"true\"] { background-color:rgba(242,244,198,60); border:2px solid #D8B257; }");
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("成就列表"), page_);
    title->setStyleSheet("font-size:18px; font-weight:bold; color:#F7FFF7; background:transparent; border:none;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    auto* scroll = new QScrollArea(page_);
    scroll->setWidgetResizable(true);
    scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }");
    auto* content = new QWidget(scroll);
    content->setStyleSheet("background:transparent;");
    itemsLayout_ = new QVBoxLayout(content);
    itemsLayout_->setSpacing(10);
    itemsLayout_->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    refresh();
}

void AchievementsPageController::refresh()
{
    clearLayout(itemsLayout_);
    auto* parentWidget = itemsLayout_->parentWidget();
    const int total = achievements_.count();
    int unlockedCount = 0;
    for (int index = 0; index < total; ++index) if (achievements_.isUnlocked(index)) ++unlockedCount;
    auto* stats = new QLabel(QStringLiteral("已解锁: %1 / %2").arg(unlockedCount).arg(total), parentWidget);
    stats->setStyleSheet("color:#F7FFF7; font-weight:bold; font-size:14px; padding:8px 0;");
    itemsLayout_->addWidget(stats);
    for (int index = 0; index < total; ++index) {
        const auto& definition = achievements_.all()[index];
        const bool unlocked = achievements_.isUnlocked(index);
        auto* card = new QFrame(parentWidget);
        card->setProperty("ach", "true");
        card->setProperty("unlocked", unlocked ? "true" : "false");
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(16, 14, 16, 14);
        row->setSpacing(14);
        auto* icon = new QLabel(definition.icon, card);
        icon->setFixedSize(48, 48);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral("font-size:26px; background:%1; border-radius:24px;").arg(unlocked ? "#D8B257" : "rgba(255,255,255,30)"));
        row->addWidget(icon);
        auto* text = new QVBoxLayout;
        text->setSpacing(4);
        auto* name = new QLabel(definition.name, card);
        name->setStyleSheet(QStringLiteral("font-weight:bold; font-size:15px; color:%1; background:transparent; border:none;").arg(unlocked ? "#F7FFF7" : "rgba(247,255,247,140)"));
        text->addWidget(name);
        auto* description = new QLabel(definition.description, card);
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("font-size:12px; color:%1; background:transparent; border:none;").arg(unlocked ? "rgba(247,255,247,210)" : "rgba(247,255,247,100)"));
        text->addWidget(description);
        auto* reward = new QLabel(QStringLiteral("奖励：🪙 %1").arg(definition.rewardCoins), card);
        reward->setStyleSheet(QStringLiteral("font-size:12px; font-weight:800; color:%1; background:transparent; border:none;").arg(unlocked ? "#F2F4C6" : "rgba(242,244,198,130)"));
        text->addWidget(reward);
        row->addLayout(text, 1);
        auto* state = new QLabel(unlocked ? QStringLiteral("✓ 已获得\n奖励已发放")
                                          : QStringLiteral("🔒 未解锁\n完成条件后发放"), card);
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        state->setStyleSheet(unlocked ? "color:#D8B257; font-weight:bold; font-size:13px; background:transparent; border:none;"
                                      : "font-size:13px; background:transparent; border:none; color:rgba(247,255,247,140);");
        row->addWidget(state);
        itemsLayout_->addWidget(card);
    }
    itemsLayout_->addStretch();
}

GachaPageController::GachaPageController(CoinManager& coins, GachaManager& gacha,
                                         FocusResultService& focusResults, QWidget* parent)
    : QObject(parent), coins_(coins), gacha_(gacha), focusResults_(focusResults)
{
    page_ = new QWidget(parent);
    page_->setObjectName("gachaPage");
    page_->setStyleSheet(
        "QWidget#gachaPage { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #58AD8F, stop:0.62 #4FA286, stop:1 #4A907B); }"
        "QFrame[panel=\"true\"] { background-color:rgba(255,255,255,34); border:1px solid rgba(255,255,255,72); border-radius:8px; }"
        "QLabel[title=\"true\"] { color:#F7FFF7; font-size:20px; font-weight:900; background:transparent; border:none; }"
        "QLabel[subtle=\"true\"] { color:rgba(247,255,247,180); font-size:13px; background:transparent; border:none; }");
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 22, 26, 22);
    layout->setSpacing(16);
    auto* header = new QLabel(QStringLiteral("✨ 异色发掘"), page_);
    header->setProperty("title", "true");
    layout->addWidget(header);
    auto* body = new QHBoxLayout;
    body->setSpacing(16);
    auto* digPanel = new QFrame(page_);
    digPanel->setProperty("uiCard", true);
    auto* dig = new QVBoxLayout(digPanel);
    dig->setContentsMargins(24, 22, 24, 22);
    dig->setSpacing(12);
    dig->addStretch();
    auto* icon = new QLabel(QStringLiteral("⛏️"), digPanel);
    icon->setAlignment(Qt::AlignCenter);
    icon->setStyleSheet("font-size:72px; color:#F7FFF7; background:transparent; border:none;");
    dig->addWidget(icon);
    auto* cost = new QLabel(QStringLiteral("每次发掘消耗 🪙 %1").arg(GachaManager::pullCost()), digPanel);
    cost->setAlignment(Qt::AlignCenter); cost->setProperty("subtle", "true"); dig->addWidget(cost);
    coinLabel_ = new QLabel(digPanel); coinLabel_->setAlignment(Qt::AlignCenter); coinLabel_->setProperty("subtle", "true"); dig->addWidget(coinLabel_);
    refundLabel_ = new QLabel(digPanel); refundLabel_->setAlignment(Qt::AlignCenter); refundLabel_->setProperty("subtle", "true"); dig->addWidget(refundLabel_);
    auto* digButton = new QPushButton(QStringLiteral("开始发掘"), digPanel);
    digButton->setCursor(Qt::PointingHandCursor); digButton->setMinimumHeight(46);
    digButton->setObjectName("btnPrimary");
    digButton->setProperty("testId", QStringLiteral("gachaPullButton"));
    dig->addWidget(digButton);
    resultIcon_ = new QLabel(digPanel); resultIcon_->setFixedHeight(100); resultIcon_->setAlignment(Qt::AlignCenter); resultIcon_->setStyleSheet("font-size:48px; background:transparent; border:none;"); dig->addWidget(resultIcon_);
    resultTitle_ = new QLabel(QStringLiteral("准备发掘新的异色树种"), digPanel); resultTitle_->setObjectName("gachaResultTitle"); resultTitle_->setMinimumHeight(28); resultTitle_->setAlignment(Qt::AlignCenter); resultTitle_->setWordWrap(true); resultTitle_->setStyleSheet("color:#F7FFF7; font-size:17px; font-weight:900; background:transparent; border:none;"); dig->addWidget(resultTitle_);
    resultDescription_ = new QLabel(QStringLiteral("重复获得会返还 🪙 %1").arg(GachaManager::duplicateCoinRefund()), digPanel); resultDescription_->setMinimumHeight(54); resultDescription_->setAlignment(Qt::AlignCenter); resultDescription_->setWordWrap(true); resultDescription_->setProperty("subtle", "true"); dig->addWidget(resultDescription_);
    dig->addStretch();
    auto* collectionPanel = new QFrame(page_);
    collectionPanel->setProperty("uiCard", true);
    auto* collection = new QVBoxLayout(collectionPanel);
    collection->setContentsMargins(18, 16, 18, 16); collection->setSpacing(10);
    auto* collectionTitle = new QLabel(QStringLiteral("异色收藏"), collectionPanel); collectionTitle->setProperty("title", "true"); collection->addWidget(collectionTitle);
    auto* scroll = new QScrollArea(collectionPanel); scroll->setWidgetResizable(true); scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }");
    auto* content = new QWidget(scroll); content->setStyleSheet("background:transparent; border:none;");
    variantsLayout_ = new QVBoxLayout(content); variantsLayout_->setContentsMargins(0, 0, 0, 0); variantsLayout_->setSpacing(8);
    scroll->setWidget(content); collection->addWidget(scroll, 1);
    body->addWidget(digPanel, 3); body->addWidget(collectionPanel, 4); layout->addLayout(body, 1);
    connect(digButton, &QPushButton::clicked, this, [this]() {
        if (coins_.balance() < static_cast<uint32_t>(GachaManager::pullCost())) {
            DialogPresenter::information(page_, QStringLiteral("余额不足"), QStringLiteral("需要 🪙 %1 才能发掘。").arg(GachaManager::pullCost()));
            return;
        }
        const auto result = gacha_.pull(coins_);
        if (!result.success) {
            resultIcon_->setText(QStringLiteral("!"));
            resultTitle_->setText(QStringLiteral("发掘未完成"));
            resultDescription_->setText(QStringLiteral("本次没有获得新异色，请稍后重试。"));
            refresh();
            return;
        }
        for (const auto& variant : gacha_.allVariants()) {
            if (variant.id != result.variantId) continue;
            const QPixmap pixmap = variantPixmap(variant, QSize(92, 92));
            if (!pixmap.isNull()) resultIcon_->setPixmap(pixmap); else resultIcon_->setText(result.icon);
            break;
        }
        if (result.isNew) {
            resultTitle_->setText(QStringLiteral("新异色获得"));
            resultDescription_->setText(QStringLiteral("%1（%2）已解锁\n%3").arg(result.displayName, result.rarity, result.description));
        } else {
            resultTitle_->setText(QStringLiteral("重复获得，已返还金币"));
            resultDescription_->setText(QStringLiteral("%1，返还 🪙 %2\n%3").arg(result.displayName).arg(result.coinRefund).arg(result.description));
        }
        refresh();
        focusResults_.evaluateAchievements();
        emit achievementsChanged();
    });
    refresh();
}

void GachaPageController::refresh()
{
    coinLabel_->setText(QStringLiteral("当前金币：🪙 %1").arg(coins_.balance()));
    refundLabel_->setText(QStringLiteral("重复异色返还：🪙 %1").arg(GachaManager::duplicateCoinRefund()));
    clearLayout(variantsLayout_);
    for (const auto& variant : gacha_.allVariants()) {
        auto* row = new QFrame;
        row->setProperty("uiCard", true);
        auto* layout = new QHBoxLayout(row); layout->setContentsMargins(12, 10, 12, 10); layout->setSpacing(10);
        auto* icon = new QLabel(row); icon->setAlignment(Qt::AlignCenter); icon->setFixedSize(58, 58); icon->setStyleSheet("background:transparent; border:none;");
        if (variant.unlocked) {
            const QPixmap pixmap = variantPixmap(variant, QSize(52, 52));
            if (!pixmap.isNull()) icon->setPixmap(pixmap); else { icon->setText(variant.icon); icon->setStyleSheet("font-size:24px; background:transparent; border:none;"); }
        } else { icon->setText(QStringLiteral("？")); icon->setStyleSheet("font-size:24px; color:rgba(247,255,247,150); background:transparent; border:none;"); }
        auto* text = new QVBoxLayout; text->setContentsMargins(0, 0, 0, 0); text->setSpacing(3);
        auto* name = new QLabel(variant.unlocked ? variant.displayName : QStringLiteral("未发现异色"), row); name->setStyleSheet("color:#F7FFF7; font-size:14px; font-weight:800; background:transparent; border:none;"); text->addWidget(name);
        if (variant.unlocked) { auto* description = new QLabel(variant.description, row); description->setWordWrap(true); description->setStyleSheet("color:rgba(247,255,247,175); font-size:12px; font-weight:500; background:transparent; border:none;"); text->addWidget(description); }
        auto* state = new QLabel(variant.unlocked ? QStringLiteral("%1\n已解锁").arg(variant.rarity) : QStringLiteral("待发现"), row); state->setAlignment(Qt::AlignRight | Qt::AlignVCenter); state->setStyleSheet("color:rgba(247,255,247,170); font-size:12px; background:transparent; border:none;");
        layout->addWidget(icon); layout->addLayout(text, 1); layout->addWidget(state); variantsLayout_->addWidget(row);
    }
    variantsLayout_->addStretch();
}
