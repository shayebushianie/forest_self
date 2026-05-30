#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>

class RuleEngine;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(RuleEngine& ruleEngine, QWidget* parent = nullptr);

    uint32_t selectedPlantType() const;
    uint32_t selectedMinutes() const;
    uint32_t selectedTagId() const;
    bool isStopwatchMode() const;
    bool isGentleMode() const;

private slots:
    void onAddBlacklist();
    void onRemoveBlacklist();
    void onModeChanged(int index);

private:
    RuleEngine& ruleEngine_;
    QComboBox* plantCombo_;
    QSpinBox* minutesSpin_;
    QComboBox* tagCombo_;
    QComboBox* modeCombo_;
    QComboBox* focusModeCombo_;
    QListWidget* blacklistWidget_;
    QLineEdit* blacklistInput_;
};

#endif // SETTINGSDIALOG_H
