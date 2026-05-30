#include "ui/HistoryWidget.h"
#include "ui/TimelineItemWidget.h"
#include <QVBoxLayout>

HistoryWidget::HistoryWidget(DatabaseManager& db, QWidget* parent)
    : QWidget(parent), db_(db)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    listWidget_ = new QListWidget(this);
    listWidget_->setStyleSheet(
        "QListWidget { background:transparent; border:none; }"
        "QListWidget::item { background:transparent; }"
        "QListWidget::item:selected { background:transparent; }");
    layout->addWidget(listWidget_);

    refreshHistory();
}

void HistoryWidget::refreshHistory()
{
    listWidget_->clear();
    auto records = db_.getAllRecords();
    if (records.empty()) return;

    int total = static_cast<int>(records.size());
    for (int i = total - 1; i >= 0; --i) {
        bool isFirst = (i == total - 1);
        bool isLast = (i == 0);

        auto* item = new QListWidgetItem(listWidget_);
        auto* widget = new TimelineItemWidget(records[i], isFirst, isLast, this);
        item->setSizeHint(QSize(0, 56));
        listWidget_->addItem(item);
        listWidget_->setItemWidget(item, widget);
    }
}
