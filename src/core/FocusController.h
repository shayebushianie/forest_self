#ifndef FOCUSCONTROLLER_H
#define FOCUSCONTROLLER_H

#include <QObject>
#include <memory>
#include <cstdint>
#include "plant/AbstractPlant.h"
#include "storage/DatabaseManager.h"

// Owns the focus-session state machine. UI code sends user actions here, while
// SystemMonitor reports app violations here; the controller persists the final
// FocusRecord when a session reaches a terminal state.
class FocusController : public QObject {
    Q_OBJECT

public:
    enum class State : uint32_t {
        IDLE = 0, RUNNING = 1, WARNING = 2, PAUSED = 3, SUCCESS = 4, FAILED = 5
    };

    enum class TimerMode : uint32_t { COUNTDOWN = 0, STOPWATCH = 1 };
    enum class FocusMode : uint8_t  { STRICT_MODE = 0, GENTLE_MODE = 1 };

    explicit FocusController(DatabaseManager& db, QObject* parent = nullptr);
    ~FocusController() override = default;

    void startFocus(uint32_t plantType, uint32_t minutes,
                    TimerMode timerMode = TimerMode::COUNTDOWN,
                    FocusMode focusMode = FocusMode::STRICT_MODE,
                    uint32_t tagId = 0,
                    bool allowPause = true,
                    bool autoExtendCountdown = false);
    void pauseFocus();
    void resumeFocus();
    void abandonFocus();
    void completeFocus();

    void handleViolationDetected(const QString& appName);
    void handleSafeWindowDetected();

    // Called once per second by the application timer.
    void tick();

    State currentState() const { return currentState_; }
    TimerMode timerMode() const { return currentMode_; }
    FocusMode focusMode() const { return focusMode_; }
    uint32_t remainingSeconds() const { return remainingSeconds_; }
    uint32_t actualSeconds() const { return actualSeconds_; }
    uint32_t violationCount() const { return violationCount_; }
    uint32_t lastCoinsEarned() const { return lastCoinsEarned_; }
    bool allowPause() const { return allowPause_; }
    bool autoExtendCountdown() const { return autoExtendCountdown_; }

signals:
    void sig_stateChanged(FocusController::State newState);
    void sig_focusStarted(uint32_t recordId);
    void sig_focusFinalized(uint32_t recordId, uint32_t status);
    void sig_tick(uint32_t displaySeconds, bool isStopwatch);
    void sig_growthStageChanged(uint32_t stage);
    void sig_strictWarningTick(uint32_t remainingWarnSeconds);
    void sig_softViolation(const QString& appName);

private:
    // Terminal transitions. Both eventually update the existing running record.
    void handleSuccess();
    void handleFailure();
    void checkpointRecord();

    // Keeps plant growth and UI growth-stage signals in sync with elapsed time.
    void updateGrowth();
    uint32_t calculateCoins() const;
    void writeRecord(FocusRecordStatus status);

    State       currentState_ = State::IDLE;
    TimerMode   currentMode_ = TimerMode::COUNTDOWN;
    FocusMode   focusMode_ = FocusMode::STRICT_MODE;
    uint32_t    plannedMinutes_ = 0;
    uint32_t    remainingSeconds_ = 0;
    uint32_t    actualSeconds_ = 0;
    uint32_t    violationCount_ = 0;
    uint32_t    violationSeconds_ = 0;
    uint32_t    warningRemainingSeconds_ = 10;
    uint32_t    recordIndex_ = 0;
    uint32_t    lastCheckpointSeconds_ = 0;
    uint32_t    currentTagId_ = 0;
    uint32_t    lastCoinsEarned_ = 0;
    uint32_t    lastPublishedGrowthStage_ = 0;
    bool        allowPause_ = true;
    bool        autoExtendCountdown_ = false;

    std::unique_ptr<AbstractPlant> currentPlant_;
    DatabaseManager& db_;
};

#endif // FOCUSCONTROLLER_H
