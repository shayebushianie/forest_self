#include "core/FocusController.h"
#include "plant/OakTree.h"
#include "plant/PineTree.h"
#include "plant/Rose.h"
#include <ctime>
#include <iostream>

FocusController::FocusController(DatabaseManager& db, QObject* parent)
    : QObject(parent), db_(db)
{
}

void FocusController::startFocus(uint32_t plantType, uint32_t minutes,
                                  TimerMode timerMode, FocusMode focusMode)
{
    if (currentState_ != State::IDLE) return;
    if (timerMode == TimerMode::COUNTDOWN && (minutes < 10 || minutes > 120)) return;

    currentMode_ = timerMode;
    focusMode_ = focusMode;
    actualSeconds_ = 0;
    violationCount_ = 0;
    warningRemainingSeconds_ = 10;

    plannedMinutes_ = (timerMode == TimerMode::STOPWATCH) ? 0 : minutes;
    remainingSeconds_ = (timerMode == TimerMode::COUNTDOWN) ? minutes * 60 : 0;

    switch (plantType) {
    case 0: currentPlant_.reset(new OakTree()); break;
    case 1: currentPlant_.reset(new PineTree()); break;
    case 2: currentPlant_.reset(new Rose()); break;
    default: return;
    }

    FocusRecord record;
    record.plantType = plantType;
    record.plannedMinutes = plannedMinutes_;
    record.status = 3;
    record.focusMode = static_cast<uint8_t>(focusMode);
    record.startTimestamp = static_cast<uint64_t>(std::time(nullptr));
    recordIndex_ = db_.append(record);

    currentState_ = State::RUNNING;
    emit sig_stateChanged(currentState_);

    const char* modeNames[] = {"STRICT","GENTLE"};
    std::cout << "[FocusController] Focus started: "
              << currentPlant_->getPlantName()
              << " [" << modeNames[static_cast<int>(focusMode_)] << "]"
              << (timerMode == TimerMode::STOPWATCH ? " [STOPWATCH]" : "")
              << "\n";
}

void FocusController::pauseFocus()
{
    if (currentState_ != State::RUNNING) return;
    currentState_ = State::PAUSED;
    emit sig_stateChanged(currentState_);
}

void FocusController::resumeFocus()
{
    if (currentState_ != State::PAUSED) return;
    currentState_ = State::RUNNING;
    emit sig_stateChanged(currentState_);
}

void FocusController::abandonFocus()
{
    if (currentState_ != State::RUNNING && currentState_ != State::PAUSED
        && currentState_ != State::WARNING) return;
    if (currentPlant_) currentPlant_->wither();
    writeRecord(2);
}

void FocusController::completeFocus()
{
    if (currentState_ != State::RUNNING) return;
    if (currentMode_ != TimerMode::STOPWATCH) return;
    if (actualSeconds_ < 600) { handleFailure(); }
    else                      { handleSuccess(); }
}

void FocusController::handleViolationDetected(const QString&)
{
    if (currentState_ != State::RUNNING && currentState_ != State::WARNING) return;

    if (focusMode_ == FocusMode::STRICT_MODE) {
        if (currentState_ == State::RUNNING) {
            currentState_ = State::WARNING;
            warningRemainingSeconds_ = 10;
            emit sig_stateChanged(currentState_);
            emit sig_strictWarningTick(warningRemainingSeconds_);
        }
    } else {
        ++violationCount_;
        emit sig_softViolation(QString());
    }
}

void FocusController::handleSafeWindowDetected()
{
    if (focusMode_ == FocusMode::STRICT_MODE && currentState_ == State::WARNING) {
        currentState_ = State::RUNNING;
        warningRemainingSeconds_ = 10;
        emit sig_stateChanged(currentState_);
    }
}

void FocusController::tick()
{
    if (currentState_ == State::WARNING) {
        if (warningRemainingSeconds_ > 0) --warningRemainingSeconds_;
        emit sig_strictWarningTick(warningRemainingSeconds_);
        if (warningRemainingSeconds_ == 0) { handleFailure(); }
        return;
    }

    if (currentState_ != State::RUNNING) return;

    ++actualSeconds_;

    if (currentMode_ == TimerMode::COUNTDOWN) {
        if (remainingSeconds_ > 0) --remainingSeconds_;
        emit sig_tick(remainingSeconds_, false);
        if (remainingSeconds_ == 0) { handleSuccess(); }
    } else {
        emit sig_tick(actualSeconds_, true);
        if (actualSeconds_ >= 7200) { handleSuccess(); }
    }

    updateGrowth();
}

void FocusController::updateGrowth()
{
    if (!currentPlant_) return;
    int total = (currentMode_ == TimerMode::COUNTDOWN)
        ? static_cast<int>(plannedMinutes_ * 60) : 1500;
    currentPlant_->grow(static_cast<int>(actualSeconds_), total);
    emit sig_growthStageChanged(currentPlant_->getGrowthStage());
}

void FocusController::handleSuccess()
{
    writeRecord(0);
    uint32_t coins = calculateCoins();
    std::cout << "[FocusController] Success! Coins earned: " << coins << "\n";
}

void FocusController::handleFailure()
{
    if (currentPlant_) currentPlant_->wither();
    writeRecord(1);
}

void FocusController::writeRecord(uint32_t status)
{
    currentState_ = (status == 0) ? State::SUCCESS : State::FAILED;
    emit sig_stateChanged(currentState_);

    FocusRecord record;
    record.recordId = recordIndex_;
    record.plantType = currentPlant_ ? currentPlant_->getPlantType() : 0;
    record.plannedMinutes = plannedMinutes_;
    record.actualSeconds = actualSeconds_;
    record.status = status;
    record.violationCount = violationCount_;
    record.growthStage = currentPlant_ ? currentPlant_->getGrowthStage() : 4;
    record.coinsEarned = (status == 0) ? calculateCoins() : 0;
    record.tagId = 0;
    record.focusMode = static_cast<uint8_t>(focusMode_);
    db_.updateById(recordIndex_, record);
}

uint32_t FocusController::calculateCoins() const
{
    uint32_t coins = actualSeconds_ / 300;
    if (focusMode_ == FocusMode::GENTLE_MODE && violationCount_ > 0) {
        coins /= 2;
    }
    return coins;
}
