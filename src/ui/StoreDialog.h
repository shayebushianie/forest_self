#ifndef STOREDIALOG_H
#define STOREDIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QWidget>
#include <cstdint>

class CoinManager;

class StoreDialog : public QDialog {
    Q_OBJECT

public:
    explicit StoreDialog(CoinManager& coinManager, QWidget* parent = nullptr);

private:
    void rebuildItems();

    CoinManager& coinManager_;
    QVBoxLayout* listLayout_ = nullptr;
    QWidget* scrollContent_ = nullptr;
};

#endif // STOREDIALOG_H
