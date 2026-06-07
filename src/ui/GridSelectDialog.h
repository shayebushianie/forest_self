#ifndef GRIDSELECTDIALOG_H
#define GRIDSELECTDIALOG_H

#include <QDialog>
#include <QGridLayout>
#include <QPushButton>
#include <vector>
#include "storage/DatabaseManager.h"

class GridSelectDialog : public QDialog {
    Q_OBJECT

public:
    GridSelectDialog(DatabaseManager& db, QWidget* parent = nullptr);

    uint8_t getSelectedGridIndex() const { return selectedGridIndex_; }

private:
    DatabaseManager& db_;
    uint8_t selectedGridIndex_ = 0;
};

#endif // GRIDSELECTDIALOG_H
