#ifndef HISTORYWIDGET_H
#define HISTORYWIDGET_H

#include <QWidget>
#include <QListWidget>
#include "storage/DatabaseManager.h"

class HistoryWidget : public QWidget {
    Q_OBJECT

public:
    explicit HistoryWidget(DatabaseManager& db, QWidget* parent = nullptr);

    void refreshHistory();

private:
    DatabaseManager& db_;
    QListWidget* listWidget_ = nullptr;
};

#endif // HISTORYWIDGET_H
