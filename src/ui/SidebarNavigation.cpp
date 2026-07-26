#include "ui/SidebarNavigation.h"

#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QWidget>

SidebarNavigation::SidebarNavigation(QWidget* sidebar, QStackedWidget* pages,
                                     QPushButton* toggleButton, QPushButton* coinButton,
                                     QVector<Item> items)
    : sidebar_(sidebar), pages_(pages), toggleButton_(toggleButton), coinButton_(coinButton),
      items_(std::move(items))
{
}

bool SidebarNavigation::switchTo(int pageIndex, const std::function<void(int)>& onPageActivated)
{
    if (!pages_ || pageIndex < 0 || pageIndex >= pages_->count()) return false;
    pages_->setCurrentIndex(pageIndex);
    onPageActivated(pageIndex);
    refreshActiveState(pageIndex);
    return true;
}

void SidebarNavigation::toggle(uint32_t coinBalance)
{
    if (!sidebar_) return;
    const int endWidth = expanded_ ? 60 : 180;
    expanded_ = !expanded_;
    coinBalance_ = coinBalance;

    sidebar_->setFixedWidth(endWidth);
    applyLabels();
}

void SidebarNavigation::setReducedMotion(bool reducedMotion)
{
    Q_UNUSED(reducedMotion);
}

void SidebarNavigation::setCoinBalance(uint32_t coinBalance)
{
    coinBalance_ = coinBalance;
    if (coinButton_) {
        coinButton_->setText(QStringLiteral("  🪙 %1").arg(coinBalance_));
    }
}

void SidebarNavigation::applyLabels()
{
    if (toggleButton_) {
        toggleButton_->setText(expanded_ ? QStringLiteral("  ☰  收起菜单") : QStringLiteral("  ☰"));
    }
    for (const Item& item : items_) {
        if (item.button) item.button->setText(expanded_ ? item.expandedText : item.collapsedText);
    }
    if (toggleButton_) {
        toggleButton_->setAccessibleName(expanded_ ? QStringLiteral("Collapse navigation")
                                                   : QStringLiteral("Expand navigation"));
    }
    for (const Item& item : items_) {
        if (item.button) item.button->setAccessibleName(item.expandedText);
    }
    setCoinBalance(coinBalance_);
}

void SidebarNavigation::refreshActiveState(int pageIndex)
{
    for (const Item& item : items_) {
        if (!item.button) continue;
        item.button->setProperty("active", item.pageIndex == pageIndex ? "true" : "false");
        item.button->style()->unpolish(item.button);
        item.button->style()->polish(item.button);
    }
}
