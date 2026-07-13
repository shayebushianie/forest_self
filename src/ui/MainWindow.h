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
#include <QCheckBox>
#include <QFont>
#include <memory>
#include "core/FocusResultService.h"
#include "core/FocusSessionCoordinator.h"
#include "ui/CommercePages.h"
#include "ui/SocialPages.h"
#include "ui/SidebarNavigation.h"

class FocusController;
class SystemMonitor;
class RuleEngine;
class CoinManager;
class QuoteProvider;
class DatabaseManager;
class TimerRing;
class GardenCanvas;
class ForestDashboardWidget;
class ChallengeDashboardWidget;
class GuardianDashboardWidget;
class PlantTimerWidget;
class SettingsDialog;
class AchievementEngine;
class DashboardSnapshotService;
class GachaManager;
class ForestLayoutManager;
class FriendManager;
class ChallengeManager;
class TagManager;
class GuardianManager;
class UserFeatureServices;
class QProgressBar;
class QSpinBox;
class QTabWidget;
class QAction;

// Main application shell. It wires the controller, monitor, storage, wallet,
// presets, achievements, and all page widgets together.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(FocusController& controller,
                        SystemMonitor& monitor,
                        RuleEngine& ruleEngine,
                        CoinManager& coinManager,
                        QuoteProvider& quotes,
                        DatabaseManager& db,
                        AchievementEngine& achievements,
                        FocusResultService& focusResults,
                        DashboardSnapshotService& dashboardSnapshots,
                        UserFeatureServices& featureServices,
                        QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStartClicked();
    void onPauseResumeClicked();
    void onAbandonClicked();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onOpenGacha();
    void onOpenAchievements();
    void onOpenMyForest();
    void onOpenFriends();
    void onOpenChallenges();
    void onOpenGuardian();

private:
    // Navigation helpers for the left sidebar and stacked pages.
    void initNavigationLayout();
    void switchPage(int index);
    void refreshPlantCombo();
    void refreshShopPage();
    void refreshAchievementsPage();
    void refreshGachaPage();
    void refreshFriendBrowsePage(const QString& filter = QString());
    void refreshFriendRequestsPage();
    void refreshFriendsPage();
    void refreshChallengePage();
    void refreshTagOptions();
    void refreshGuardianPage();

    // Refresh helpers that translate domain state into visible UI state.
    void updateUI();
    void refreshGarden();
    void setupTrayIcon();
    void updateTrayStatus();
    void applyAccessibilityPreferences();
    void setHomeTimerMode(bool stopwatch);
    void showHomeModeOptions(bool stopwatch);
    void hideHomeModeOptions();
    void refreshHomeModeControls();
    void restoreFocusSetup();
    void persistFocusSetup();
    void refreshHomeTagBadge();
    void setSelectedTagId(uint32_t tagId);
    void showHomePlantSelector();
    void showPauseBreakOverlay();
    void hidePauseBreakOverlay();
    void resumeFromPauseBreak();
    void updatePauseBreakText();
    void handleFocusFinalized(uint32_t recordId, uint32_t status,
                              const FocusResultService::Outcome& outcome);
    void showFocusResult(uint32_t recordId, const FocusResultService::Outcome& outcome,
                         bool completed);

    // These services are owned by main() and live for the whole application.
    FocusController& controller_;
    SystemMonitor& monitor_;
    RuleEngine& ruleEngine_;
    CoinManager& coinManager_;
    QuoteProvider& quotes_;
    DatabaseManager& db_;
    AchievementEngine& achievements_;
    FocusResultService& focusResults_;
    DashboardSnapshotService& dashboardSnapshots_;
    std::unique_ptr<FocusSessionCoordinator> focusSession_;
    std::unique_ptr<ShopPageController> shopPage_;
    std::unique_ptr<AchievementsPageController> achievementsPage_;
    std::unique_ptr<GachaPageController> gachaPage_;
    std::unique_ptr<FriendsPageController> friendsPage_;
    GachaManager* m_gacha = nullptr;
    ForestLayoutManager* m_forestLayout = nullptr;
    FriendManager* m_friendMgr = nullptr;
    ChallengeManager* m_challengeMgr = nullptr;
    TagManager* m_tagMgr = nullptr;
    GuardianManager* m_guardian = nullptr;

    // Main page widgets. Qt parent ownership handles deletion.
    PlantTimerWidget* timerRing_;
    GardenCanvas* gardenCanvas_;
    ForestDashboardWidget* forestDashboard_ = nullptr;
    ChallengeDashboardWidget* challengeDashboard_ = nullptr;
    QPushButton *startBtn_, *pauseBtn_, *abandonBtn_;
    QSystemTrayIcon* trayIcon_;
    QAction* trayPauseAction_ = nullptr;
    QAction* trayAbandonAction_ = nullptr;
    QTimer quoteTimer_;
    QFont baseApplicationFont_;
    QWidget* warningOverlay_ = nullptr;
    QLabel* warningLabel_ = nullptr;
    QLabel* coinLabel_ = nullptr;
    QPushButton* sidebarCoinLabel_ = nullptr;
    QWidget* countdownOptionsWidget_ = nullptr;
    QWidget* homeModeOverlay_ = nullptr;
    QWidget* homeModePanel_ = nullptr;
    QWidget* pauseBreakOverlay_ = nullptr;
    QLabel* pauseBreakTimeLabel_ = nullptr;
    QPushButton* pauseBreakContinueBtn_ = nullptr;
    QTimer pauseBreakTimer_;
    int pauseBreakRemainingSeconds_ = 0;
    QPushButton* homeCountdownBtn_ = nullptr;
    QPushButton* homeStopwatchBtn_ = nullptr;
    QLabel* homeCountdownModeLabel_ = nullptr;
    QLabel* homeStopwatchModeLabel_ = nullptr;
    QCheckBox* homeAllowPauseCheck_ = nullptr;
    QCheckBox* homeDeepFocusCheck_ = nullptr;
    QCheckBox* homeGroupPlantCheck_ = nullptr;
    QCheckBox* homeAutoExtendCheck_ = nullptr;

    QWidget* sidebarWidget_ = nullptr;
    QStackedWidget* stackedWidget_ = nullptr;
    QPushButton *btnToggle_, *btnHome_, *btnForest_, *btnShop_, *btnGacha_, *btnFriends_, *btnChallenges_, *btnGuardian_, *btnAchievements_, *btnSettings_;
    std::unique_ptr<SidebarNavigation> sidebarNavigation_;

    QComboBox* settingsPlantCombo_ = nullptr;
    QComboBox* settingsModeCombo_ = nullptr;
    QCheckBox* settingsDeepFocusCheck_ = nullptr;
    QCheckBox* settingsAllowPauseCheck_ = nullptr;
    QSpinBox* settingsMinutesSpin_ = nullptr;
    QComboBox* settingsTagCombo_ = nullptr;
    QListWidget* settingsBlacklistWidget_ = nullptr;
    QLineEdit* settingsBlacklistInput_ = nullptr;
    QLineEdit* settingsOathInput_ = nullptr;


    GuardianDashboardWidget* guardianDashboard_ = nullptr;
};

#endif // MAINWINDOW_H
