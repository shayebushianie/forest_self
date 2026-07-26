#include "ui/CommercePages.h"

#include "config/PlantCatalog.h"
#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/FocusResultService.h"
#include "core/GachaManager.h"
#include "ui/GachaDigSiteWidget.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"

#include <QColor>
#include <QFrame>
#include <QGridLayout>
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

void clearLayout(QLayout* layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

void clearDetachedCardGrid(QGridLayout* layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        delete item->widget();
        delete item;
    }
}

QPixmap lockedVariantPixmap(const GachaManager::VariantDef& variant, const QSize& targetSize)
{
    QPixmap pixmap = variantPixmap(variant, targetSize);
    if (pixmap.isNull()) return {};
    QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const int alpha = qAlpha(line[x]);
            if (alpha == 0) continue;
            const int shade = (qRed(line[x]) + qGreen(line[x]) + qBlue(line[x])) / 3;
            const int muted = qBound(72, shade / 3 + 72, 132);
            line[x] = qRgba(muted - 18, muted, muted - 10, qMin(alpha, 150));
        }
    }
    return QPixmap::fromImage(image);
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
        "QWidget#achievementsPage { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #58AD8F, stop:0.65 #4FA286, stop:1 #4A907B); }");
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
        card->setObjectName("achievementCard");
        card->setStyleSheet(unlocked
            ? "QFrame#achievementCard { background-color:rgba(255,250,220,238); border:2px solid #B68B2E; border-radius:10px; }"
            : "QFrame#achievementCard { background-color:rgba(247,255,247,225); border:1px solid rgba(45,91,70,90); border-radius:10px; }");
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
        name->setStyleSheet(QStringLiteral("font-weight:bold; font-size:15px; color:%1; background:transparent; border:none;").arg(unlocked ? "#5A4518" : "#244538"));
        text->addWidget(name);
        auto* description = new QLabel(definition.description, card);
        description->setObjectName("achievementDescription");
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("font-size:12px; color:%1; background:transparent; border:none;").arg(unlocked ? "#5A4518" : "#315747"));
        text->addWidget(description);
        auto* reward = new QLabel(QStringLiteral("奖励：🪙 %1").arg(definition.rewardCoins), card);
        reward->setStyleSheet(QStringLiteral("font-size:12px; font-weight:800; color:%1; background:transparent; border:none;").arg(unlocked ? "#8A641B" : "#315747"));
        text->addWidget(reward);
        row->addLayout(text, 1);
        auto* state = new QLabel(unlocked ? QStringLiteral("✓ 已获得\n奖励已发放")
                                          : QStringLiteral("🔒 未解锁\n完成条件后发放"), card);
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        state->setStyleSheet(unlocked ? "color:#5A4518; font-weight:bold; font-size:13px; background:transparent; border:none;"
                                      : "font-size:13px; background:transparent; border:none; color:#315747;");
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
        "QWidget#gachaPage { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        " stop:0 #3D8A72, stop:0.52 #58AA8C, stop:1 #78BDA6); }"
        "QFrame#gachaDigPanel, QFrame#gachaCollectionPanel { background:rgba(244,249,224,34);"
        " border:1px solid rgba(236,247,216,92); border-radius:20px; }"
        "QFrame#gachaResultCard { background:rgba(249,250,225,205); border:1px solid rgba(82,126,91,92);"
        " border-radius:14px; }"
        "QLabel[gachaTitle=\"true\"] { color:#F8F8D9; font-size:22px; font-weight:900;"
        " background:transparent; border:none; }"
        "QLabel[gachaSubtitle=\"true\"] { color:rgba(244,249,226,190); font-size:13px;"
        " background:transparent; border:none; }"
        "QLabel[gachaMetric=\"true\"] { color:#315747; background:rgba(249,250,225,210);"
        " border:1px solid rgba(72,121,88,70); border-radius:12px; padding:7px 11px; font-weight:800; }"
        "QFrame#gachaVariantCard { background:rgba(250,250,228,226);"
        " border:1px solid rgba(70,118,84,82); border-radius:15px; }"
        "QFrame#gachaVariantCard:hover { background:#FAFBE7; border-color:#4E9B72; }"
        "QFrame#gachaVariantCard QLabel { background:transparent; border:none; }");
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 20, 26, 22);
    layout->setSpacing(14);

    auto* headerRow = new QHBoxLayout;
    auto* headerText = new QVBoxLayout;
    headerText->setSpacing(2);
    auto* header = new QLabel(QStringLiteral("异色发掘"), page_);
    header->setProperty("gachaTitle", "true");
    auto* subtitle = new QLabel(QStringLiteral("消耗金币随机获取异色植物。"), page_);
    subtitle->setProperty("gachaSubtitle", "true");
    headerText->addWidget(header);
    headerText->addWidget(subtitle);
    headerRow->addLayout(headerText);
    headerRow->addStretch();
    collectionProgress_ = new QLabel(page_);
    collectionProgress_->setProperty("gachaMetric", "true");
    headerRow->addWidget(collectionProgress_);
    layout->addLayout(headerRow);

    auto* body = new QHBoxLayout;
    body->setSpacing(18);

    auto* digPanel = new QFrame(page_);
    digPanel->setObjectName("gachaDigPanel");
    auto* dig = new QVBoxLayout(digPanel);
    dig->setContentsMargins(16, 16, 16, 16);
    dig->setSpacing(10);
    digSite_ = new GachaDigSiteWidget(digPanel);
    dig->addWidget(digSite_, 1);

    auto* metrics = new QHBoxLayout;
    metrics->setSpacing(8);
    auto* cost = new QLabel(QStringLiteral("单次消耗 %1").arg(GachaManager::pullCost()), digPanel);
    cost->setProperty("gachaMetric", "true");
    cost->setAlignment(Qt::AlignCenter);
    coinLabel_ = new QLabel(digPanel);
    coinLabel_->setProperty("gachaMetric", "true");
    coinLabel_->setAlignment(Qt::AlignCenter);
    refundLabel_ = new QLabel(digPanel);
    refundLabel_->setProperty("gachaMetric", "true");
    refundLabel_->setAlignment(Qt::AlignCenter);
    metrics->addWidget(cost);
    metrics->addWidget(coinLabel_);
    metrics->addWidget(refundLabel_);
    dig->addLayout(metrics);

    auto* digButton = new QPushButton(QStringLiteral("开始发掘"), digPanel);
    digButton->setCursor(Qt::PointingHandCursor);
    digButton->setMinimumHeight(48);
    digButton->setObjectName("btnPrimary");
    digButton->setProperty("testId", QStringLiteral("gachaPullButton"));
    dig->addWidget(digButton);

    auto* resultCard = new QFrame(digPanel);
    resultCard->setObjectName("gachaResultCard");
    auto* resultLayout = new QVBoxLayout(resultCard);
    resultLayout->setContentsMargins(16, 11, 16, 12);
    resultLayout->setSpacing(3);
    resultTitle_ = new QLabel(QStringLiteral("等待发掘"), resultCard);
    resultTitle_->setObjectName("gachaResultTitle");
    resultTitle_->setAlignment(Qt::AlignCenter);
    resultTitle_->setStyleSheet("color:#315747; font-size:17px; font-weight:900; background:transparent;");
    resultDescription_ = new QLabel(QStringLiteral("点击开始发掘，结果将显示在发掘地。"), resultCard);
    resultDescription_->setMinimumHeight(34);
    resultDescription_->setAlignment(Qt::AlignCenter);
    resultDescription_->setWordWrap(true);
    resultDescription_->setStyleSheet("color:#567264; font-size:12px; font-weight:600; background:transparent;");
    resultLayout->addWidget(resultTitle_);
    resultLayout->addWidget(resultDescription_);
    dig->addWidget(resultCard);

    auto* collectionPanel = new QFrame(page_);
    collectionPanel->setObjectName("gachaCollectionPanel");
    auto* collection = new QVBoxLayout(collectionPanel);
    collection->setContentsMargins(16, 14, 16, 16);
    collection->setSpacing(10);
    auto* collectionTitle = new QLabel(QStringLiteral("异色图鉴"), collectionPanel);
    collectionTitle->setProperty("gachaTitle", "true");
    collectionTitle->setStyleSheet("font-size:18px;");
    collection->addWidget(collectionTitle);
    auto* scroll = new QScrollArea(collectionPanel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }"
                          "QScrollArea > QWidget > QWidget { background:transparent; }");
    auto* content = new QWidget(scroll);
    content->setObjectName("gachaCollectionGrid");
    content->setStyleSheet("QWidget#gachaCollectionGrid { background:transparent; border:none; }");
    variantsLayout_ = new QGridLayout(content);
    variantsLayout_->setContentsMargins(2, 2, 2, 2);
    variantsLayout_->setHorizontalSpacing(10);
    variantsLayout_->setVerticalSpacing(10);
    for (int column = 0; column < 4; ++column) variantsLayout_->setColumnStretch(column, 1);
    scroll->setWidget(content);
    collection->addWidget(scroll, 1);

    body->addWidget(digPanel, 5);
    body->addWidget(collectionPanel, 6);
    layout->addLayout(body, 1);

    connect(digButton, &QPushButton::clicked, this, [this]() {
        if (coins_.balance() < static_cast<uint32_t>(GachaManager::pullCost())) {
            DialogPresenter::information(page_, QStringLiteral("余额不足"),
                                         QStringLiteral("发掘需要 %1 金币。").arg(GachaManager::pullCost()));
            return;
        }
        const auto result = gacha_.pull(coins_);
        if (!result.success) {
            digSite_->clearDiscoveredPlant();
            resultTitle_->setText(QStringLiteral("发掘失败"));
            resultDescription_->setText(QStringLiteral("未能保存本次发掘结果。"));
            refresh();
            return;
        }
        const GachaManager::VariantDef* discovered = nullptr;
        for (const auto& variant : gacha_.allVariants()) {
            if (variant.id != result.variantId) continue;
            discovered = &variant;
            break;
        }
        if (discovered) {
            const QColor accent(discovered->tintColor);
            digSite_->setDiscoveredPlant(variantPixmap(*discovered, QSize(190, 190)), accent);
        }
        if (result.isNew) {
            resultTitle_->setText(QStringLiteral("获得新异色"));
            resultDescription_->setText(QStringLiteral("%1 · %2\n%3")
                                            .arg(result.displayName, result.rarity, result.description));
        } else {
            resultTitle_->setText(QStringLiteral("获得重复异色"));
            resultDescription_->setText(QStringLiteral("%1 · 已返还 %2 金币\n%3")
                                            .arg(result.displayName).arg(result.coinRefund).arg(result.description));
        }
        refresh();
        focusResults_.evaluateAchievements();
        emit achievementsChanged();
    });
    refresh();
    for (const auto& variant : gacha_.allVariants()) {
        if (!variant.unlocked) continue;
        const QColor accent(variant.tintColor);
        digSite_->setDiscoveredPlant(variantPixmap(variant, QSize(190, 190)), accent);
        resultTitle_->setText(QStringLiteral("已发现异色"));
        resultDescription_->setText(QStringLiteral("%1 · %2\n%3")
                                        .arg(variant.displayName, variant.rarity, variant.description));
        break;
    }
}

void GachaPageController::refresh()
{
    coinLabel_->setText(QStringLiteral("金币 %1").arg(coins_.balance()));
    refundLabel_->setText(QStringLiteral("重复返还 %1").arg(GachaManager::duplicateCoinRefund()));
    collectionProgress_->setText(QStringLiteral("已发现 %1 / %2")
                                     .arg(gacha_.unlockedCount()).arg(gacha_.variantCount()));
    clearDetachedCardGrid(variantsLayout_);
    QWidget* grid = variantsLayout_->parentWidget();
    int index = 0;
    for (const auto& variant : gacha_.allVariants()) {
        auto* card = new QFrame(grid);
        card->setObjectName("gachaVariantCard");
        card->setProperty("gachaVariantCard", true);
        card->setMinimumSize(118, 150);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(9, 9, 9, 10);
        cardLayout->setSpacing(4);
        auto* icon = new QLabel(card);
        icon->setAlignment(Qt::AlignCenter);
        icon->setMinimumHeight(82);
        icon->setStyleSheet("background:transparent; border:none;");
        if (variant.unlocked) {
            icon->setPixmap(variantPixmap(variant, QSize(80, 80)));
        } else {
            icon->setPixmap(lockedVariantPixmap(variant, QSize(76, 76)));
        }
        auto* name = new QLabel(variant.unlocked ? variant.displayName : QStringLiteral("未发现"), card);
        name->setAlignment(Qt::AlignCenter);
        name->setWordWrap(true);
        name->setStyleSheet("color:#315747; font-size:13px; font-weight:900; background:transparent;");
        cardLayout->addWidget(icon, 1);
        cardLayout->addWidget(name);
        if (variant.unlocked) {
            auto* state = new QLabel(variant.rarity, card);
            state->setAlignment(Qt::AlignCenter);
            state->setStyleSheet(QStringLiteral("color:%1; font-size:11px; font-weight:700;"
                                                " background:transparent;")
                                     .arg(variant.tintColor));
            cardLayout->addWidget(state);
        }
        variantsLayout_->addWidget(card, index / 4, index % 4);
        ++index;
    }
    variantsLayout_->setRowStretch((index + 3) / 4, 1);
}
