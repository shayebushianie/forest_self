#ifndef ACHIEVEMENTDIALOG_H
#define ACHIEVEMENTDIALOG_H

#include <QDialog>
#include "ui/DialogPresenter.h"

class AchievementEngine;

class AchievementDialog : public QDialog {
    Q_OBJECT

public:
    explicit AchievementDialog(AchievementEngine* mgr, QWidget* parent = nullptr);

private:
    void setupUI();

    AchievementEngine* m_mgr;
};

#endif
