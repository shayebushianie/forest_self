#include "ui/SidebarNavigation.h"
#include "config/UserPreferences.h"

#include <QAbstractAnimation>
#include <QPropertyAnimation>
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
    const int startWidth = expanded_ ? 180 : 60;
    const int endWidth = expanded_ ? 60 : 180;
    expanded_ = !expanded_;
    coinBalance_ = coinBalance;
    for (auto* animation : sidebar_->findChildren<QPropertyAnimation*>(QString(), Qt::FindDirectChildrenOnly)) {
        animation->stop();
        animation->deleteLater();
    }
    if (UserPreferences::instance().accessibilityOptions().reducedMotion) {
        sidebar_->setFixedWidth(endWidth);
        applyLabels();
        return;
    }

    auto* minAnimation = new QPropertyAnimation(sidebar_, "minimumWidth", sidebar_);
    minAnimation->setDuration(220);
    minAnimation->setStartValue(startWidth);
    minAnimation->setEndValue(endWidth);
    minAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    auto* maxAnimation = new QPropertyAnimation(sidebar_, "maximumWidth", sidebar_);
    maxAnimation->setDuration(220);
    maxAnimation->setStartValue(startWidth);
    maxAnimation->setEndValue(endWidth);
    maxAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    minAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    maxAnimation->start(QAbstractAnimation::DeleteWhenStopped);
    applyLabels();
}

void SidebarNavigation::setCoinBalance(uint32_t coinBalance)
{
    coinBalance_ = coinBalance;
    if (coinButton_) {
        coinButton_->setText(expanded_ ? QStringLiteral("  🪙 %1").arg(coinBalance_) : QStringLiteral("🪙"));
        coinButton_->setToolTip(QStringLiteral("金币详情：%1 金币").arg(coinBalance_));
        coinButton_->setAccessibleName(coinButton_->toolTip());
    }
}

void SidebarNavigation::applyLabels()
{
    if (toggleButton_) {
        toggleButton_->setText(expanded_ ? QStringLiteral("  ☰  收起菜单") : QStringLiteral("  ☰"));
        toggleButton_->setToolTip(expanded_ ? QStringLiteral("收起菜单") : QStringLiteral("展开菜单"));
        toggleButton_->setAccessibleName(toggleButton_->toolTip());
    }
    for (const Item& item : items_) {
        if (item.button) {
            item.button->setText(expanded_ ? item.expandedText : item.collapsedText.trimmed());
            item.button->setToolTip(item.expandedText.trimmed());
            item.button->setAccessibleName(item.expandedText.trimmed());
        }
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
