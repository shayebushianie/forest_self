#ifndef STATISTICSDIALOG_H
#define STATISTICSDIALOG_H

#include <QDialog>
#include <map>
#include <cstdint>
#include "storage/DatabaseManager.h"

class StatisticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit StatisticsDialog(DatabaseManager& db, QWidget* parent = nullptr);

private:
    DatabaseManager& db_;
};

#endif // STATISTICSDIALOG_H
