#ifndef STOREDIALOG_H
#define STOREDIALOG_H

#include <QDialog>
#include <cstdint>

class CoinManager;

class StoreDialog : public QDialog {
    Q_OBJECT

public:
    explicit StoreDialog(CoinManager& coinManager, QWidget* parent = nullptr);
};

#endif // STOREDIALOG_H
