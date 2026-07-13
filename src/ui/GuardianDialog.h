#ifndef GUARDIANDIALOG_H
#define GUARDIANDIALOG_H

#include <QDialog>
#include "ui/DialogPresenter.h"
#include <QLabel>
#include <QProgressBar>
#include <QSpinBox>
#include "core/GuardianManager.h"

class GuardianDialog : public QDialog {
    Q_OBJECT
public:
    explicit GuardianDialog(GuardianManager* mgr, QWidget* parent = nullptr);

private:
    GuardianManager* m_mgr;
    QLabel* m_progressNum;
    QLabel* m_progressPct;
    QProgressBar* m_progress;
    QLabel* m_curStreakVal;
    QLabel* m_longestStreakVal;
    QLabel* m_totalVal;
    QSpinBox* m_goalSpin;
    void refresh();
};

#endif
