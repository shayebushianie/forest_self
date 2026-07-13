#include "core/FocusSessionCoordinator.h"

FocusSessionCoordinator::FocusSessionCoordinator(FocusController& controller,
                                                 FocusResultService& results,
                                                 QObject* parent)
    : QObject(parent), controller_(controller), results_(results)
{
    connect(&controller_, &FocusController::sig_focusStarted, this, [this](uint32_t recordId) {
        if (results_.registerPendingRecord(recordId)) return;
        lastStartRegistrationSucceeded_ = false;
        emit startRegistrationFailed();
        controller_.abandonFocus();
    });
    connect(&controller_, &FocusController::sig_focusFinalized, this,
            [this](uint32_t recordId, uint32_t status) {
                emit terminalResultReady(recordId, status, results_.applyTerminalRecord(recordId));
            });
}

bool FocusSessionCoordinator::start(const StartRequest& request)
{
    lastStartRegistrationSucceeded_ = true;
    controller_.startFocus(request.plantType, request.plannedMinutes, request.timerMode,
                           request.focusMode, request.tagId, request.allowPause, request.autoExtend);
    return lastStartRegistrationSucceeded_ &&
           controller_.currentState() == FocusController::State::RUNNING;
}

void FocusSessionCoordinator::pause()
{
    controller_.pauseFocus();
}

void FocusSessionCoordinator::resume()
{
    controller_.resumeFocus();
}

void FocusSessionCoordinator::abandon()
{
    controller_.abandonFocus();
}
