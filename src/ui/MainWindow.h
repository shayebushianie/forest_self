#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <QStackedWidget>
#include <QPropertyAnimation>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QLineEdit>
#include <memory>

#include "storage/PresetManager.h"

class FocusController;
class SystemMonitor;
class RuleEngine;
class CoinManager;
class QuoteProvider;
class DatabaseManager;
class TimerRing;
class GardenCanvas;
class SettingsDialog;
class AchievementEngine;
class HistoryWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(FocusController& controller,
                        SystemMonitor& monitor,
                        RuleEngine& ruleEngine,
                        CoinManager& coinManager,
                        QuoteProvider& quotes,
                        DatabaseManager& db,
                        PresetManager& presets,
                        AchievementEngine& achievements,
                        QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStartClicked();
    void onPauseResumeClicked();
    void onAbandonClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void initNavigationLayout();
    void toggleSidebar();
    void switchPage(int index);
    void updateUI();
    void refreshGarden();
    void setupTrayIcon();
    void startPresetFocus(uint32_t index);

    FocusController& controller_;
    SystemMonitor& monitor_;
    RuleEngine& ruleEngine_;
    CoinManager& coinManager_;
    QuoteProvider& quotes_;
    DatabaseManager& db_;
    PresetManager& presets_;
    AchievementEngine& achievements_;

    TimerRing* timerRing_;
    GardenCanvas* gardenCanvas_;
    QPushButton *startBtn_, *pauseBtn_, *abandonBtn_;
    QSystemTrayIcon* trayIcon_;
    QTimer quoteTimer_;
    QWidget* warningOverlay_ = nullptr;
    QLabel* warningLabel_ = nullptr;

    QWidget* sidebarWidget_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    QPushButton *btnToggle_, *btnHome_, *btnForest_, *btnShop_, *btnSettings_;
    bool isSidebarExpanded_ = true;
    HistoryWidget* historyWidget_ = nullptr;

    QComboBox* settingsPlantCombo_ = nullptr;
    QComboBox* settingsModeCombo_ = nullptr;
    QComboBox* settingsFocusModeCombo_ = nullptr;
    QSpinBox* settingsMinutesSpin_ = nullptr;
    QComboBox* settingsTagCombo_ = nullptr;
    QListWidget* settingsBlacklistWidget_ = nullptr;
    QLineEdit* settingsBlacklistInput_ = nullptr;
};

#endif // MAINWINDOW_H
