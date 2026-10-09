#include "ui/CommercePages.h"

#include "config/PlantCatalog.h"
#include "config/UserPreferences.h"
#include "core/AchievementEngine.h"
#include "core/CoinManager.h"
#include "core/FocusResultService.h"
#include "core/GachaManager.h"
#include "ui/AppStyle.h"
#include "ui/PlantImageUtils.h"
#include "ui/DialogPresenter.h"

#include <QDialog>
#include <QFrame>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

namespace {

QPixmap variantPixmap(const GachaManager::VariantDef& variant, const QSize& targetSize)
{
    const QPixmap image(variant.imagePath);
    return image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void clearLayout(QVBoxLayout* layout)
{
    while (layout && layout->count() > 0) {
        QLayoutItem* item = layout->takeAt(0);
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->deleteLater();
        }
        delete item;
    }
}

} // namespace

ShopPageController::ShopPageController(CoinManager& coins, FocusResultService& focusResults, QWidget* parent)
    : QObject(parent), coins_(coins), focusResults_(focusResults)
{
    page_ = new QWidget(parent);
    page_->setObjectName("shopPage");
    page_->setStyleSheet(QStringLiteral(
        "QWidget#shopPage { background:%1; }"
        "QWidget#shopHeader, QWidget#shopContent, QWidget#shopRow "
        "{ background:transparent; border:none; }"
        "QScrollArea#shopScroll, QScrollArea#shopScroll QWidget#qt_scrollarea_viewport "
        "{ background:transparent; border:none; }"
        "QFrame#shopCard { background:%2; border:1px solid %3; border-radius:8px; }"
        "QFrame#shopCard:hover { border-color:%4; }"
        "QFrame#shopCard QLabel { color:%5; background:transparent; border:none; }"
        "QFrame#shopCard QLabel[shopName=\"true\"] { font-size:18px; font-weight:700; }"
        "QFrame#shopCard QLabel[shopPrice=\"true\"] { color:%6; font-size:14px; }"
        "QFrame#shopCard QLabel[shopState=\"true\"] { color:%4; font-size:13px; }"
        "QFrame#shopCard QLabel[shopState=\"true\"][shopOwned=\"true\"] {"
        " color:%5; background:%7; border-radius:4px; padding:2px 8px; }"
        "QFrame#shopCard QLabel[shopState=\"true\"][saveFailed=\"true\"] { color:#A63D3A; }"
        "QWidget#shopPage QLabel[shopPageTitle=\"true\"] { color:%5; font-size:28px; font-weight:700; }"
        "QWidget#shopPage QLabel[shopMeta=\"true\"] { color:%6; font-size:13px; }"
        "QWidget#shopPage QLabel[shopBalance=\"true\"] { color:%5; font-size:16px; font-weight:700; }"
        "QWidget#shopPage QPushButton { padding:0; min-height:36px; border-width:2px; }"
        "QWidget#shopPage QPushButton#btnSecondary { background:%2; border:2px solid %3; color:%5; }"
        "QWidget#shopPage QPushButton#btnSecondary:hover { background:%7; border-color:%4; }"
        "QWidget#shopPage QPushButton:disabled { border-width:2px; }"
        "QWidget#shopPage QPushButton:focus { border:2px solid %4; }")
        .arg(AppStyle::color(AppStyle::Color::Workspace).name(),
             AppStyle::color(AppStyle::Color::Surface).name(),
             AppStyle::color(AppStyle::Color::Border).name(),
             AppStyle::color(AppStyle::Color::Primary).name(),
             AppStyle::color(AppStyle::Color::Text).name(),
             AppStyle::color(AppStyle::Color::Muted).name(),
             AppStyle::color(AppStyle::Color::Selection).name()));
    AppStyle::applyAccessibility(*page_);
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 22, 26, 24);
    layout->setSpacing(14);
    auto* header = new QWidget(page_);
    header->setObjectName("shopHeader");
    header->setMaximumWidth(1008);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* headings = new QVBoxLayout;
    headings->setSpacing(4);
    auto* title = new QLabel(QStringLiteral("植物商城"), header);
    title->setProperty("shopPageTitle", true);
    collectionLabel_ = new QLabel(header);
    collectionLabel_->setProperty("shopMeta", true);
    headings->addWidget(title);
    headings->addWidget(collectionLabel_);
    headerLayout->addLayout(headings, 1);
    balanceLabel_ = new QLabel(header);
    balanceLabel_->setObjectName("shopBalanceLabel");
    balanceLabel_->setProperty("shopBalance", true);
    connect(&coins_, &CoinManager::sig_balanceChanged, this, [this](uint32_t balance) {
        balanceLabel_->setText(QStringLiteral("🪙 %1 金币").arg(balance));
    });
    headerLayout->addWidget(balanceLabel_, 0, Qt::AlignVCenter);
    auto* headerBand = new QHBoxLayout;
    headerBand->addStretch();
    headerBand->addWidget(header, 1);
    headerBand->addStretch();
    layout->addLayout(headerBand);
    auto* scroll = new QScrollArea(page_);
    scroll->setObjectName("shopScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* content = new QWidget(scroll);
    content->setObjectName("shopContent");
    content->setFixedWidth(1008);
    itemsLayout_ = new QVBoxLayout(content);
    itemsLayout_->setContentsMargins(0, 0, 0, 0);
    itemsLayout_->setSpacing(16);
    itemsLayout_->setSizeConstraint(QLayout::SetMinimumSize);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    refresh();
}

void ShopPageController::refresh()
{
    clearLayout(itemsLayout_);
    auto* parentWidget = itemsLayout_->parentWidget();
    balanceLabel_->setText(QStringLiteral("🪙 %1 金币").arg(coins_.balance()));
    int ownedCount = 0;
    QHBoxLayout* cardsRow = nullptr;
    int index = 0;
    for (const PlantDefinition& plant : PlantCatalog::all()) {
        if (index % 3 == 0) {
            auto* rowWidget = new QWidget(parentWidget);
            rowWidget->setObjectName("shopRow");
            cardsRow = new QHBoxLayout(rowWidget);
            cardsRow->setContentsMargins(0, 0, 0, 0);
            cardsRow->setSpacing(24);
            itemsLayout_->addWidget(rowWidget);
        }
        const bool owned = coins_.isPlantUnlocked(plant.type);
        if (owned) ++ownedCount;
        const bool affordable = coins_.balance() >= plant.storeCost;
        auto* card = new QFrame(parentWidget);
        card->setObjectName("shopCard");
        card->setProperty("plantType", plant.type);
        card->setAttribute(Qt::WA_Hover);
        card->setFixedWidth(320);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(4, 10, 4, 10);
        cardLayout->setSpacing(6);
        auto* image = new QLabel(card);
        image->setObjectName("plantImageSlot");
        image->setFixedSize(120, 120);
        image->setAlignment(Qt::AlignCenter);
        image->setAccessibleName(plant.displayName);
        const QPixmap pixmap = PlantImageUtils::loadPlantIcon(plant.iconPath);
        if (!pixmap.isNull()) {
            image->setPixmap(pixmap.scaled(112, 112, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        cardLayout->addWidget(image, 0, Qt::AlignHCenter);
        auto* name = new QLabel(plant.displayName, card);
        name->setProperty("shopName", true);
        name->setAlignment(Qt::AlignCenter);
        name->setToolTip(plant.displayName);
        auto* price = new QLabel(QStringLiteral("🪙 %1 金币").arg(plant.storeCost), card);
        price->setProperty("shopPrice", true);
        price->setAlignment(Qt::AlignCenter);
        auto* state = new QLabel(owned ? QStringLiteral("✓ 已拥有")
            : affordable ? QStringLiteral("可购买") : QStringLiteral("金币不足"), card);
        state->setObjectName(QStringLiteral("shopState_%1").arg(plant.type));
        state->setProperty("shopState", true);
        state->setProperty("shopOwned", owned);
        state->setWordWrap(true);
        state->setAlignment(Qt::AlignCenter);
        state->ensurePolished();
        state->setFixedHeight(QFontMetrics(state->font()).lineSpacing() * 2 + 8);
        cardLayout->addWidget(name);
        cardLayout->addWidget(price);
        cardLayout->addWidget(state);
        auto* actions = new QHBoxLayout;
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(8);
        auto* details = new QPushButton(QStringLiteral("详情"), card);
        DialogPresenter::setSecondary(details);
        details->setProperty("testId", QStringLiteral("shopDetails_%1").arg(plant.type));
        details->setFixedSize(148, 40);
        details->setCursor(Qt::PointingHandCursor);
        details->setToolTip(QStringLiteral("查看 %1").arg(plant.displayName));
        connect(details, &QPushButton::clicked, this, [this, type = plant.type] { showPlantDetails(type); });
        actions->addWidget(details);
        auto* buy = new QPushButton(card);
        DialogPresenter::setPrimary(buy);
        buy->setFixedSize(148, 40);
        buy->setProperty("testId", QStringLiteral("shopPurchase_%1").arg(plant.type));
        buy->setCursor(Qt::PointingHandCursor);
        if (owned) {
            buy->setText(QStringLiteral("已拥有"));
            buy->setEnabled(false);
            buy->setToolTip(QStringLiteral("该植物已解锁，可在专注主页中选择。"));
        } else {
            buy->setText(affordable ? QStringLiteral("解锁植物") : QStringLiteral("金币不足"));
            buy->setEnabled(affordable);
            buy->setToolTip(affordable ? QStringLiteral("使用 %1 金币解锁 %2").arg(plant.storeCost).arg(plant.displayName)
                : QStringLiteral("需要 %1 金币，当前 %2 金币。完成专注后可获得金币。")
                    .arg(plant.storeCost).arg(coins_.balance()));
            connect(buy, &QPushButton::clicked, this, [this, state, buy, type = plant.type]() {
                const auto result = coins_.purchasePlant(type);
                if (result == CoinManager::OperationResult::InsufficientFunds) {
                    showPurchaseFeedback(QStringLiteral("余额不足"), QStringLiteral("请先完成专注任务赚取 🪙。"), false);
                    return;
                }
                if (result == CoinManager::OperationResult::SaveFailed || result == CoinManager::OperationResult::Invalid) {
                    state->setText(QStringLiteral("⚠ 购买未保存\n金币和植物未改变"));
                    state->setProperty("saveFailed", true);
                    state->style()->unpolish(state);
                    state->style()->polish(state);
                    buy->setText(QStringLiteral("重试解锁"));
                    buy->setToolTip(QStringLiteral("上次购买未保存，没有扣除金币；检查数据目录后重试。"));
                    showPurchaseFeedback(QStringLiteral("购买未完成"),
                        QStringLiteral("购买未能保存，金币和植物状态均未改变。请检查数据目录后重试。"), false);
                    return;
                }
                if (result == CoinManager::OperationResult::AlreadyApplied) {
                    showPurchaseFeedback(QStringLiteral("已拥有"), QStringLiteral("该植物已解锁，无需再次购买。"), true);
                    refresh();
                    return;
                }
                focusResults_.evaluateAchievements();
                showPurchaseFeedback(QStringLiteral("解锁成功"),
                    focusResults_.lastAchievementEvaluationSucceeded() ? QStringLiteral("新植物已解锁！")
                        : QStringLiteral("新植物已解锁。成就检查未完成，下次专注结算将重试；本次购买已保存。"), true);
                refresh();
                emit plantCatalogChanged();
                emit achievementsChanged();
            });
        }
        actions->addWidget(buy);
        cardLayout->addLayout(actions);
        card->setFixedHeight(qMax(300, cardLayout->sizeHint().height()));
        cardsRow->addWidget(card);
        ++index;
    }
    if (index % 3 != 0 && cardsRow) cardsRow->addStretch(1);
    collectionLabel_->setText(QStringLiteral("已拥有 %1 / %2 种植物").arg(ownedCount).arg(PlantCatalog::all().size()));
    itemsLayout_->addStretch();
}

void ShopPageController::showPlantDetails(uint32_t plantType)
{
    const PlantDefinition& plant = PlantCatalog::byType(plantType);
    QDialog dialog(page_);
    DialogPresenter::prepare(dialog);
    dialog.setObjectName("shopPlantDetailDialog");
    dialog.setWindowTitle(plant.displayName);
    dialog.setFixedWidth(460);
    dialog.setStyleSheet(dialog.styleSheet().replace(QStringLiteral("#forestDialog"),
        QStringLiteral("#shopPlantDetailDialog")) + QStringLiteral(
        "\n"
        "QDialog#shopPlantDetailDialog { background:%1; color:%2; border:1px solid %3; border-radius:16px; }"
        "QDialog#shopPlantDetailDialog QLabel { color:%2; background:transparent; border:none; }"
        "QDialog#shopPlantDetailDialog QLabel[shopDetailTitle=\"true\"] { font-size:22px; font-weight:700; }"
        "QDialog#shopPlantDetailDialog QLabel#shopPlantDetailContent { font-size:15px; }"
        "QDialog#shopPlantDetailDialog QPushButton#btnSecondary { background:%1; color:%2; "
        "border:1px solid %3; }"
        "QDialog#shopPlantDetailDialog QPushButton#btnSecondary:hover { background:%4; border-color:%5; }")
        .arg(AppStyle::color(AppStyle::Color::Surface).name(),
             AppStyle::color(AppStyle::Color::Text).name(),
             AppStyle::color(AppStyle::Color::Border).name(),
             AppStyle::color(AppStyle::Color::Selection).name(),
             AppStyle::color(AppStyle::Color::Primary).name()));
    AppStyle::applyAccessibility(dialog);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 22);
    layout->setSpacing(16);
    auto* image = new QLabel(&dialog);
    image->setObjectName("shopPlantDetailImage");
    image->setFixedSize(128, 128);
    image->setAlignment(Qt::AlignCenter);
    const QPixmap pixmap = PlantImageUtils::loadPlantIcon(plant.iconPath);
    image->setPixmap(pixmap.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    auto* title = new QLabel(plant.displayName, &dialog);
    title->setProperty("shopDetailTitle", true);
    title->setWordWrap(true);
    title->setAlignment(Qt::AlignCenter);
    auto* content = new QLabel(QStringLiteral("价格：🪙 %1 金币\n状态：%2\n获取方式：在植物商城使用金币解锁")
        .arg(plant.storeCost).arg(coins_.isPlantUnlocked(plantType) ? QStringLiteral("已拥有") : QStringLiteral("未解锁")), &dialog);
    content->setObjectName("shopPlantDetailContent");
    content->setTextFormat(Qt::PlainText);
    content->setWordWrap(true);
    auto* close = new QPushButton(QStringLiteral("关闭"), &dialog);
    DialogPresenter::setSecondary(close);
    close->setProperty("testId", "shopPlantDetailClose");
    close->setDefault(true);
    layout->addWidget(image, 0, Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(content);
    layout->addWidget(close, 0, Qt::AlignRight);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.adjustSize();
    dialog.exec();
}

void ShopPageController::showPurchaseFeedback(const QString& title, const QString& message, bool success)
{
    if (success) DialogPresenter::information(page_, title, message);
    else DialogPresenter::warning(page_, title, message);
}

AchievementsPageController::AchievementsPageController(AchievementEngine& achievements, QWidget* parent)
    : QObject(parent), achievements_(achievements)
{
    page_ = new QWidget(parent);
    page_->setObjectName("achievementsPage");
    page_->setStyleSheet(QStringLiteral(
        "QWidget#achievementsPage { background:%1; }"
        "QWidget#achievementHeader, QWidget#achievementContent { background:transparent; border:none; }"
        "QScrollArea#achievementScroll, QScrollArea#achievementScroll QWidget#qt_scrollarea_viewport { background:transparent; border:none; }"
        "QFrame[ach=\"true\"] { background:%2; border:1px solid %3; border-radius:8px; }"
        "QFrame[ach=\"true\"] QLabel { color:%4; background:transparent; border:none; }"
        "QFrame[ach=\"true\"] QLabel[achievementTitle=\"true\"] { color:%5; font-size:18px; font-weight:700; }"
        "QLabel#achievementDescription { font-size:14px; }"
        "QLabel#achievementIcon { font-size:28px; background:%1; border-radius:8px; }"
        "QFrame[unlocked=\"true\"] QLabel#achievementIcon { background:%6; }"
        "QLabel#achievementReward { color:%5; font-size:16px; font-weight:700; }"
        "QLabel#achievementState { color:%4; font-size:13px; }"
        "QLabel[achievementPageTitle=\"true\"] { color:%5; font-size:28px; font-weight:700; }"
        "QLabel#achievementSummary { color:%4; font-size:14px; }")
        .arg(AppStyle::color(AppStyle::Color::Workspace).name(),
             AppStyle::color(AppStyle::Color::Surface).name(),
             AppStyle::color(AppStyle::Color::Border).name(),
             AppStyle::color(AppStyle::Color::Muted).name(),
             AppStyle::color(AppStyle::Color::Text).name(),
             AppStyle::color(AppStyle::Color::Selection).name()));
    AppStyle::applyAccessibility(*page_);
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 22, 26, 24);
    layout->setSpacing(18);
    auto* header = new QWidget(page_);
    header->setObjectName("achievementHeader");
    header->setMaximumWidth(1160);
    auto* headings = new QVBoxLayout(header);
    headings->setContentsMargins(0, 0, 0, 0);
    headings->setSpacing(6);
    auto* title = new QLabel(QStringLiteral("成就列表"), header);
    title->setProperty("achievementPageTitle", true);
    summaryLabel_ = new QLabel(header);
    summaryLabel_->setObjectName("achievementSummary");
    headings->addWidget(title);
    headings->addWidget(summaryLabel_);
    auto* headerBand = new QHBoxLayout;
    headerBand->addStretch();
    headerBand->addWidget(header, 1);
    headerBand->addStretch();
    layout->addLayout(headerBand);
    auto* scroll = new QScrollArea(page_);
    scroll->setObjectName("achievementScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* content = new QWidget(scroll);
    content->setObjectName("achievementContent");
    content->setMaximumWidth(1160);
    itemsLayout_ = new QVBoxLayout(content);
    itemsLayout_->setSizeConstraint(QLayout::SetMinimumSize);
    itemsLayout_->setSpacing(12);
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
    summaryLabel_->setText(QStringLiteral("已解锁 %1 / %2 项 · 解锁时自动发放金币").arg(unlockedCount).arg(total));
    QList<QLabel*> rewardLabels;
    int rewardWidth = 132;
    for (int index = 0; index < total; ++index) {
        const auto& definition = achievements_.all()[index];
        const bool unlocked = achievements_.isUnlocked(index);
        auto* card = new QFrame(parentWidget);
        card->setProperty("ach", "true");
        card->setProperty("achievementId", definition.id);
        card->setProperty("unlocked", unlocked);
        card->setMinimumHeight(124);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(18, 16, 18, 16);
        row->setSpacing(18);
        auto* icon = new QLabel(definition.icon, card);
        icon->setObjectName("achievementIcon");
        icon->setFixedSize(56, 56);
        icon->setAlignment(Qt::AlignCenter);
        row->addWidget(icon);
        auto* text = new QVBoxLayout;
        text->setSpacing(8);
        auto* name = new QLabel(definition.name, card);
        name->setProperty("achievementTitle", true);
        name->setWordWrap(true);
        text->addWidget(name);
        auto* description = new QLabel(definition.description, card);
        description->setObjectName("achievementDescription");
        description->setTextFormat(Qt::PlainText);
        description->setWordWrap(true);
        text->addWidget(description);
        row->addLayout(text, 1);
        row->setAlignment(text, Qt::AlignVCenter);
        auto* reward = new QLabel(QStringLiteral("🪙 %1 金币").arg(definition.rewardCoins), card);
        reward->setObjectName("achievementReward");
        reward->ensurePolished();
        rewardWidth = qMax(rewardWidth, QFontMetrics(reward->font()).horizontalAdvance(reward->text()) + 12);
        rewardLabels.append(reward);
        reward->setAlignment(Qt::AlignCenter);
        auto* state = new QLabel(unlocked ? QStringLiteral("✓ 已解锁\n奖励已发放") : QStringLiteral("🔒 未解锁"), card);
        state->setObjectName("achievementState");
        state->ensurePolished();
        state->setFixedWidth(qMax(124, QFontMetrics(state->font()).horizontalAdvance(QStringLiteral("奖励已发放")) + 12));
        state->setWordWrap(true);
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(reward);
        row->addWidget(state);
        itemsLayout_->addWidget(card);
    }
    for (QLabel* reward : rewardLabels) reward->setFixedWidth(rewardWidth);
    itemsLayout_->addStretch();
}

GachaPageController::GachaPageController(CoinManager& coins, GachaManager& gacha,
                                         FocusResultService& focusResults, QWidget* parent)
    : QObject(parent), coins_(coins), gacha_(gacha), focusResults_(focusResults)
{
    page_ = new QWidget(parent);
    page_->setObjectName("gachaPage");
    page_->setProperty("resultState", "ready");
    page_->setStyleSheet(QStringLiteral(
        "QWidget#gachaPage { background:%1; }"
        "QWidget#gachaContent, QWidget#gachaCollection, QWidget#gachaVariants, QWidget#variantRow "
        "{ background:transparent; border:none; }"
        "QScrollArea#gachaScroll, QScrollArea#gachaWorkspaceScroll, "
        "QWidget#gachaPage QWidget#qt_scrollarea_viewport { background:transparent; border:none; }"
        "QWidget#gachaPage QFrame[uiCard=\"true\"] { background:%2; border:1px solid %3; border-radius:8px; }"
        "QWidget#gachaPage QLabel { color:%4; background:transparent; border:none; font-size:14px; }"
        "QWidget#gachaPage QLabel[gachaPageTitle=\"true\"] { color:%5; font-size:28px; font-weight:700; }"
        "QWidget#gachaPage QLabel[title=\"true\"] { color:%5; font-size:20px; font-weight:700; }"
        "QWidget#gachaPage QLabel[variantName=\"true\"] { color:%5; font-size:16px; font-weight:700; }"
        "QWidget#gachaPage QLabel#variantDescription { font-size:13px; }"
        "QWidget#gachaPage QLabel#variantState { color:%4; font-size:13px; }"
        "QWidget#gachaPage QLabel#gachaCoinBalance { color:%5; font-size:18px; font-weight:700; }"
        "QWidget#gachaPage QLabel#gachaResultTitle { color:%5; font-size:18px; font-weight:700; }"
        "QWidget#gachaPage[resultState=\"error\"] QLabel#gachaResultTitle { color:#9A514A; }"
        "QWidget#gachaPage QLabel#variantIcon { font-size:44px; color:%4; }"
        "QWidget#gachaPage QLabel#gachaResultIcon { font-size:56px; }"
        "QWidget#gachaPage QPushButton#btnPrimary { background:%6; color:#FFFFFF; "
        "border:2px solid %6; border-radius:8px; padding:0; min-height:40px; }"
        "QWidget#gachaPage QPushButton#btnPrimary:hover { background:%7; border-color:%7; }"
        "QWidget#gachaPage QPushButton#btnPrimary:pressed { background:%8; border-color:%8; }"
        "QWidget#gachaPage QPushButton#btnPrimary:focus { border:2px solid %5; }")
        .arg(AppStyle::color(AppStyle::Color::Workspace).name(),
             AppStyle::color(AppStyle::Color::Surface).name(),
             AppStyle::color(AppStyle::Color::Border).name(),
             AppStyle::color(AppStyle::Color::Muted).name(),
             AppStyle::color(AppStyle::Color::Text).name(),
             AppStyle::color(AppStyle::Color::Primary).name(),
             AppStyle::color(AppStyle::Color::PrimaryHover).name(),
             AppStyle::color(AppStyle::Color::PrimaryPressed).name()));
    AppStyle::applyAccessibility(*page_);
    auto* layout = new QVBoxLayout(page_);
    layout->setContentsMargins(26, 22, 26, 24);
    auto* workspace = new QScrollArea(page_);
    workspace->setObjectName("gachaWorkspaceScroll");
    workspace->setWidgetResizable(true);
    workspace->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    workspace->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto* content = new QWidget(workspace);
    content->setObjectName("gachaContent");
    content->setMaximumWidth(1160);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(18);
    auto* header = new QLabel(QStringLiteral("异色发掘"), content);
    header->setProperty("gachaPageTitle", true);
    contentLayout->addWidget(header);
    auto* body = new QHBoxLayout;
    body->setSpacing(20);
    auto* digPanel = new QFrame(content);
    digPanel->setObjectName("gachaDigPanel");
    digPanel->setFixedWidth(320);
    digPanel->setProperty("uiCard", true);
    auto* dig = new QVBoxLayout(digPanel);
    dig->setContentsMargins(20, 20, 20, 20);
    dig->setSpacing(12);
    auto* digTitle = new QLabel(QStringLiteral("发掘异色"), digPanel);
    digTitle->setProperty("title", true);
    digTitle->setAlignment(Qt::AlignCenter);
    dig->addWidget(digTitle);
    auto* cost = new QLabel(QStringLiteral("每次发掘消耗 🪙 %1").arg(GachaManager::pullCost()), digPanel);
    cost->setAlignment(Qt::AlignCenter);
    dig->addWidget(cost);
    coinLabel_ = new QLabel(digPanel);
    coinLabel_->setObjectName("gachaCoinBalance");
    coinLabel_->setAlignment(Qt::AlignCenter);
    dig->addWidget(coinLabel_);
    refundLabel_ = new QLabel(digPanel);
    refundLabel_->setAlignment(Qt::AlignCenter);
    dig->addWidget(refundLabel_);
    auto* digButton = new QPushButton(QStringLiteral("开始发掘"), digPanel);
    digButton->setCursor(Qt::PointingHandCursor);
    digButton->setFixedHeight(48);
    digButton->setToolTip(QStringLiteral("消耗 %1 金币发掘一株异色植物；重复获得返还 %2 金币")
        .arg(GachaManager::pullCost()).arg(GachaManager::duplicateCoinRefund()));
    DialogPresenter::setPrimary(digButton);
    digButton->setProperty("testId", QStringLiteral("gachaPullButton"));
    dig->addWidget(digButton);
    resultIcon_ = new QLabel(QStringLiteral("⛏️"), digPanel);
    resultIcon_->setObjectName("gachaResultIcon");
    resultIcon_->setFixedSize(144, 144);
    resultIcon_->setAlignment(Qt::AlignCenter);
    dig->addWidget(resultIcon_, 0, Qt::AlignHCenter);
    resultTitle_ = new QLabel(QStringLiteral("准备发掘新的异色树种"), digPanel);
    resultTitle_->setObjectName("gachaResultTitle");
    resultTitle_->ensurePolished();
    resultTitle_->setMinimumHeight(QFontMetrics(resultTitle_->font()).lineSpacing() * 2);
    resultTitle_->setAlignment(Qt::AlignCenter);
    resultTitle_->setWordWrap(true);
    dig->addWidget(resultTitle_);
    resultDescription_ = new QLabel(QStringLiteral("等待发掘"), digPanel);
    resultDescription_->setObjectName("gachaResultDescription");
    resultDescription_->ensurePolished();
    resultDescription_->setMinimumHeight(QFontMetrics(resultDescription_->font()).lineSpacing() * 4);
    resultDescription_->setAlignment(Qt::AlignCenter);
    resultDescription_->setTextFormat(Qt::PlainText);
    resultDescription_->setWordWrap(true);
    dig->addWidget(resultDescription_);
    auto* collectionPanel = new QWidget(content);
    collectionPanel->setObjectName("gachaCollection");
    auto* collection = new QVBoxLayout(collectionPanel);
    collection->setContentsMargins(0, 0, 0, 0);
    collection->setSpacing(12);
    auto* collectionTitle = new QLabel(QStringLiteral("异色收藏"), collectionPanel);
    collectionTitle->setProperty("title", true);
    collection->addWidget(collectionTitle);
    collectionLabel_ = new QLabel(collectionPanel);
    collectionLabel_->setObjectName("gachaCollectionSummary");
    collection->addWidget(collectionLabel_);
    auto* scroll = new QScrollArea(collectionPanel);
    scroll->setObjectName("gachaScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumHeight(480);
    auto* variants = new QWidget(scroll);
    variants->setObjectName("gachaVariants");
    variantsLayout_ = new QVBoxLayout(variants);
    variantsLayout_->setSizeConstraint(QLayout::SetMinimumSize);
    variantsLayout_->setContentsMargins(0, 0, 0, 0);
    variantsLayout_->setSpacing(10);
    scroll->setWidget(variants);
    collection->addWidget(scroll, 1);
    body->addWidget(digPanel, 0, Qt::AlignTop);
    body->addWidget(collectionPanel, 1);
    contentLayout->addLayout(body, 1);
    workspace->setWidget(content);
    layout->addWidget(workspace, 1);
    connect(digButton, &QPushButton::clicked, this, [this]() {
        const auto result = gacha_.pull(coins_);
        page_->setProperty("resultState", result.success ? result.isNew ? "new" : "duplicate" : "error");
        resultTitle_->style()->unpolish(resultTitle_);
        resultTitle_->style()->polish(resultTitle_);
        if (!result.success) {
            resultIcon_->setText(QStringLiteral("⚠"));
            resultTitle_->setText(QStringLiteral("上次发掘未完成"));
            resultDescription_->setText(result.error.isEmpty() ? QStringLiteral("本次操作尚未完成，请稍后重试。") : result.error);
            refresh();
            return;
        }
        for (const auto& variant : gacha_.allVariants()) {
            if (variant.id != result.variantId) continue;
            const QPixmap pixmap = variantPixmap(variant, QSize(132, 132));
            if (!pixmap.isNull()) resultIcon_->setPixmap(pixmap); else resultIcon_->setText(result.icon);
            break;
        }
        if (result.isNew) {
            resultTitle_->setText(QStringLiteral("新异色获得"));
            resultDescription_->setText(QStringLiteral("%1（%2）已解锁\n%3").arg(result.displayName, result.rarity, result.description));
        } else {
            resultTitle_->setText(QStringLiteral("重复获得，已返还金币"));
            resultDescription_->setText(QStringLiteral("%1（%2）\n返还 🪙 %3 金币\n%4")
                .arg(result.displayName, result.rarity).arg(result.coinRefund).arg(result.description));
        }
        focusResults_.evaluateAchievements();
        if (!focusResults_.lastAchievementEvaluationSucceeded())
            resultDescription_->setText(resultDescription_->text() + QStringLiteral("\n成就检查未完成，下次专注结算将重试；本次发掘已保存。"));
        refresh();
        emit achievementsChanged();
    });
    refresh();
}

void GachaPageController::refresh()
{
    coinLabel_->setText(QStringLiteral("当前金币：🪙 %1").arg(coins_.balance()));
    refundLabel_->setText(QStringLiteral("重复异色返还：🪙 %1").arg(GachaManager::duplicateCoinRefund()));
    collectionLabel_->setText(QStringLiteral("已发现 %1 / %2 种").arg(gacha_.unlockedCount()).arg(gacha_.variantCount()));
    clearLayout(variantsLayout_);
    const int columns = UserPreferences::instance().accessibilityOptions().fontScalePercent > 120 ? 2 : 3;
    const int minimumCardWidth = columns == 2 ? 220 : 200;
    QHBoxLayout* cardsRow = nullptr;
    QList<QFrame*> cards;
    int cardHeight = 280;
    int index = 0;
    for (const auto& variant : gacha_.allVariants()) {
        if (index % columns == 0) {
            auto* rowWidget = new QWidget(variantsLayout_->parentWidget());
            rowWidget->setObjectName("variantRow");
            cardsRow = new QHBoxLayout(rowWidget);
            cardsRow->setContentsMargins(0, 0, 0, 0);
            cardsRow->setSpacing(12);
            variantsLayout_->addWidget(rowWidget);
        }
        auto* row = new QFrame(variantsLayout_->parentWidget());
        row->setProperty("uiCard", true);
        row->setProperty("variantId", variant.id);
        row->setProperty("unlocked", variant.unlocked);
        row->setMinimumWidth(minimumCardWidth);
        auto* layout = new QVBoxLayout(row);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->setSpacing(8);
        auto* icon = new QLabel(row);
        icon->setObjectName("variantIcon");
        icon->setAlignment(Qt::AlignCenter);
        icon->setFixedSize(120, 120);
        if (variant.unlocked) {
            const QPixmap pixmap = variantPixmap(variant, QSize(112, 112));
            if (!pixmap.isNull()) icon->setPixmap(pixmap); else icon->setText(variant.icon);
            icon->setAccessibleName(variant.displayName);
        } else {
            icon->setText(QStringLiteral("？"));
            icon->setAccessibleName(QStringLiteral("未发现异色"));
        }
        layout->addWidget(icon, 0, Qt::AlignHCenter);
        auto* name = new QLabel(variant.unlocked ? variant.displayName : QStringLiteral("未发现异色"), row);
        name->setProperty("variantName", true);
        name->setWordWrap(true);
        name->setAlignment(Qt::AlignCenter);
        layout->addWidget(name);
        auto* state = new QLabel(variant.unlocked ? QStringLiteral("%1 · ✓ 已发现").arg(variant.rarity) : QStringLiteral("未发现"), row);
        state->setObjectName("variantState");
        state->setWordWrap(true);
        state->setAlignment(Qt::AlignCenter);
        layout->addWidget(state);
        int textHeight = 0;
        for (QLabel* label : {name, state}) {
            label->ensurePolished();
            textHeight += QFontMetrics(label->font()).boundingRect(
                QRect(0, 0, minimumCardWidth - 32, 1000), Qt::TextWordWrap, label->text()).height();
        }
        if (variant.unlocked) {
            auto* description = new QLabel(variant.description, row);
            description->setObjectName("variantDescription");
            description->setTextFormat(Qt::PlainText);
            description->setWordWrap(true);
            description->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
            description->ensurePolished();
            const int height = QFontMetrics(description->font()).boundingRect(
                QRect(0, 0, minimumCardWidth - 32, 1000), Qt::TextWordWrap, description->text()).height();
            description->setMinimumHeight(height);
            textHeight += height;
            layout->addWidget(description);
        }
        layout->addStretch();
        cardHeight = qMax(cardHeight, 120 + textHeight + 28 + 32);
        cards.append(row);
        cardsRow->addWidget(row, 1);
        ++index;
    }
    if (index % columns != 0 && cardsRow) cardsRow->addStretch();
    for (QFrame* card : cards) card->setFixedHeight(cardHeight);
    variantsLayout_->addStretch();
}
