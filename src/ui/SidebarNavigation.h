#ifndef SIDEBAR_NAVIGATION_H
#define SIDEBAR_NAVIGATION_H

#include <QVector>
#include <QString>
#include <cstdint>
#include <functional>

class QPushButton;
class QStackedWidget;
class QWidget;

// Owns side bar labels, active styling, page selection, and collapse state.
class SidebarNavigation final {
public:
    struct Item {
        QPushButton* button = nullptr;
        int pageIndex = -1;
        QString expandedText;
        QString collapsedText;
    };

    SidebarNavigation(QWidget* sidebar, QStackedWidget* pages, QPushButton* toggleButton,
                      QPushButton* coinButton, QVector<Item> items);

    bool switchTo(int pageIndex, const std::function<void(int)>& onPageActivated);
    void toggle(uint32_t coinBalance);
    void setCoinBalance(uint32_t coinBalance);
    void setReducedMotion(bool reducedMotion);

private:
    void applyLabels();
    void refreshActiveState(int pageIndex);

    QWidget* sidebar_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QPushButton* toggleButton_ = nullptr;
    QPushButton* coinButton_ = nullptr;
    QVector<Item> items_;
    bool expanded_ = true;
    uint32_t coinBalance_ = 0;
};

#endif // SIDEBAR_NAVIGATION_H
