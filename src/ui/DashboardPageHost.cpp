#include "ui/DashboardPageHost.h"

#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>
#include <QWidget>

QWidget* DashboardPageHost::create(QWidget* dashboard, const QString& objectName,
                                   const QString& styleSheet, bool scrollable, QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(objectName);
    page->setStyleSheet(styleSheet);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    if (!scrollable) {
        dashboard->setParent(page);
        layout->addWidget(dashboard);
        return page;
    }

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea { background:transparent; border:none; }"
                          "QScrollArea > QWidget > QWidget { background:transparent; }");
    dashboard->setParent(scroll);
    scroll->setWidget(dashboard);
    layout->addWidget(scroll);
    return page;
}
