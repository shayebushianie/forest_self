#ifndef CHALLENGEDIALOG_H
#define CHALLENGEDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QSpinBox>
#include <QDateEdit>
#include <QComboBox>
#include "core/ChallengeManager.h"

class ChallengeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChallengeDialog(ChallengeManager* mgr,
                             const QVector<uint32_t>& friendIds,
                             QWidget* parent = nullptr);

private:
    ChallengeManager* mgr_;
    QTableWidget* activeTable_;
    QTableWidget* historyTable_;
    QComboBox* friendCombo_;
    QSpinBox* minutesSpin_;
    QDateEdit* endDateEdit_;
    QPushButton* createBtn_;

    void refreshActive();
    void refreshHistory();
    void onCreateClicked();
};

#endif
