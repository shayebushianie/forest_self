#ifndef FOCUS_SESSION_COORDINATOR_H
#define FOCUS_SESSION_COORDINATOR_H

#include "core/FocusController.h"
#include "core/FocusResultService.h"

#include <QObject>

// Coordinates the durable focus lifecycle. UI code supplies start options and
// reacts to signals; record registration and terminal settlement stay here.
class FocusSessionCoordinator final : public QObject {
    Q_OBJECT

public:
    struct StartRequest {
        uint32_t plantType = 0;
        uint32_t plannedMinutes = 25;
        FocusController::TimerMode timerMode = FocusController::TimerMode::COUNTDOWN;
        FocusController::FocusMode focusMode = FocusController::FocusMode::STRICT_MODE;
        uint32_t tagId = 0;
        bool allowPause = false;
        bool autoExtend = false;
    };

    explicit FocusSessionCoordinator(FocusController& controller,
                                     FocusResultService& results,
                                     QObject* parent = nullptr);

    bool start(const StartRequest& request);
    void pause();
    void resume();
    void abandon();

signals:
    void terminalResultReady(uint32_t recordId, uint32_t status,
                             const FocusResultService::Outcome& outcome);
    void startRegistrationFailed();

private:
    FocusController& controller_;
    FocusResultService& results_;
    bool lastStartRegistrationSucceeded_ = true;
};

#endif // FOCUS_SESSION_COORDINATOR_H
