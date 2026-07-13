#ifndef FOCUS_PAGE_STATE_H
#define FOCUS_PAGE_STATE_H

#include "core/FocusController.h"

// Pure presentation state derived from the focus controller. QWidget code applies it.
struct FocusPageState {
    bool idle = true;
    bool running = false;
    bool paused = false;
    bool active = false;
    bool canPause = false;

    static FocusPageState from(const FocusController& controller)
    {
        FocusPageState state;
        const auto controllerState = controller.currentState();
        state.running = controllerState == FocusController::State::RUNNING;
        state.paused = controllerState == FocusController::State::PAUSED;
        state.idle = controllerState == FocusController::State::IDLE ||
                     controllerState == FocusController::State::SUCCESS ||
                     controllerState == FocusController::State::FAILED;
        state.active = state.running || state.paused ||
                       controllerState == FocusController::State::WARNING;
        state.canPause = state.running && controller.allowPause() &&
                         controller.timerMode() == FocusController::TimerMode::COUNTDOWN;
        return state;
    }
};

#endif // FOCUS_PAGE_STATE_H
