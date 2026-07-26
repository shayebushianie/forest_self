#ifndef COMMERCE_PAGES_H
#define COMMERCE_PAGES_H

#include <QObject>

class QWidget;
class QVBoxLayout;
class QGridLayout;
class QLabel;
class GachaDigSiteWidget;
class CoinManager;
class AchievementEngine;
class FocusResultService;
class GachaManager;

class ShopPageController final : public QObject {
    Q_OBJECT
public:
    ShopPageController(CoinManager& coins, FocusResultService& focusResults, QWidget* parent);
    QWidget* page() const { return page_; }
    void refresh();

signals:
    void plantCatalogChanged();
    void achievementsChanged();

private:
    CoinManager& coins_;
    FocusResultService& focusResults_;
    QWidget* page_ = nullptr;
    QVBoxLayout* itemsLayout_ = nullptr;
};

class AchievementsPageController final : public QObject {
    Q_OBJECT
public:
    AchievementsPageController(AchievementEngine& achievements, QWidget* parent);
    QWidget* page() const { return page_; }
    void refresh();

private:
    AchievementEngine& achievements_;
    QWidget* page_ = nullptr;
    QVBoxLayout* itemsLayout_ = nullptr;
};

class GachaPageController final : public QObject {
    Q_OBJECT
public:
    GachaPageController(CoinManager& coins, GachaManager& gacha,
                        FocusResultService& focusResults, QWidget* parent);
    QWidget* page() const { return page_; }
    void refresh();

signals:
    void achievementsChanged();

private:
    CoinManager& coins_;
    GachaManager& gacha_;
    FocusResultService& focusResults_;
    QWidget* page_ = nullptr;
    QLabel* coinLabel_ = nullptr;
    QLabel* refundLabel_ = nullptr;
    QLabel* collectionProgress_ = nullptr;
    QLabel* resultTitle_ = nullptr;
    QLabel* resultDescription_ = nullptr;
    GachaDigSiteWidget* digSite_ = nullptr;
    QGridLayout* variantsLayout_ = nullptr;
};

#endif // COMMERCE_PAGES_H
