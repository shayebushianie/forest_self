#include "core/FocusController.h"
#include "plant/PlantFactory.h"
#include <QDateTime>
#include <limits>
#include <iostream>

namespace {

uint64_t currentSystemEpochSeconds()
{
    return static_cast<uint64_t>(QDateTime::currentDateTime().toSecsSinceEpoch());
}

}

FocusController::FocusController(DatabaseManager& db, QObject* parent)
    : QObject(parent), db_(db)
{
}

void FocusController::startFocus(uint32_t plantType, uint32_t minutes,
                                  TimerMode timerMode, FocusMode focusMode,
                                  uint32_t tagId,
                                  bool allowPause,
                                  bool autoExtendCountdown)
{
    if (currentState_ == State::RUNNING || currentState_ == State::PAUSED ||
        currentState_ == State::WARNING) return;
    if (timerMode == TimerMode::COUNTDOWN && (minutes < 10 || minutes > 120)) return;

    // Reset all per-session counters before creating the running placeholder
    // record. The final result overwrites this same record by recordIndex_.
    currentMode_ = timerMode;
    focusMode_ = focusMode;
    currentTagId_ = tagId;
    allowPause_ = allowPause;
    autoExtendCountdown_ = (timerMode == TimerMode::COUNTDOWN) ? autoExtendCountdown : false;
    lastCoinsEarned_ = 0;
    lastPublishedGrowthStage_ = 0;
    actualSeconds_ = 0;
    lastCheckpointSeconds_ = 0;
    violationCount_ = 0;
    violationSeconds_ = 0;
    warningRemainingSeconds_ = 10;

    plannedMinutes_ = (timerMode == TimerMode::STOPWATCH) ? 0 : minutes;
    remainingSeconds_ = (timerMode == TimerMode::COUNTDOWN) ? minutes * 60 : 0;

    currentPlant_ = PlantFactory::create(plantType);
    if (!currentPlant_) return;

    FocusRecord record;
    record.plantType = plantType;
    record.plannedMinutes = plannedMinutes_;
    record.status = FocusRecordStatus::Running;
    record.focusMode = focusMode == FocusMode::STRICT_MODE
        ? PersistedFocusMode::Deep : PersistedFocusMode::GentleLegacy;
    record.tagId = currentTagId_;
    record.startTimestamp = currentSystemEpochSeconds();
    recordIndex_ = db_.append(record);
    if (recordIndex_ == std::numeric_limits<uint32_t>::max()) {
        currentPlant_.reset();
        return;
    }

    currentState_ = State::RUNNING;
    emit sig_stateChanged(currentState_);
    emit sig_focusStarted(recordIndex_);
}

void FocusController::pauseFocus() {
    if (currentState_ != State::RUNNING) return;
    if (!allowPause_) return;
    checkpointRecord();
    currentState_ = State::PAUSED; emit sig_stateChanged(currentState_);
}

void FocusController::resumeFocus() {
    if (currentState_ != State::PAUSED) return;
    currentState_ = State::RUNNING; emit sig_stateChanged(currentState_);
}

void FocusController::abandonFocus() {
    if (currentState_ != State::RUNNING && currentState_ != State::PAUSED
        && currentState_ != State::WARNING) return;
    if (currentPlant_) currentPlant_->wither();
    writeRecord(FocusRecordStatus::Abandoned);
}

void FocusController::completeFocus() {
    if (currentState_ != State::RUNNING) return;
    if (currentMode_ != TimerMode::STOPWATCH) return;
    if (actualSeconds_ < 600) { handleFailure(); }
    else                      { handleSuccess(); }
}

void FocusController::handleViolationDetected(const QString&)
{
    if (currentState_ != State::RUNNING && currentState_ != State::WARNING) return;

    if (focusMode_ == FocusMode::STRICT_MODE) {
        // Strict mode gives the user a short grace period to return to safety.
        if (currentState_ == State::RUNNING) {
            currentState_ = State::WARNING;
            warningRemainingSeconds_ = 10;
            emit sig_stateChanged(currentState_);
            emit sig_strictWarningTick(warningRemainingSeconds_);
        }
    } else {
        // Gentle mode keeps the session alive but discounts reward time later.
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
        if (remainingSeconds_ == 0) {
            if (autoExtendCountdown_) {
                // Continue counting upward after the planned countdown ends.
                currentMode_ = TimerMode::STOPWATCH;
                autoExtendCountdown_ = false;
                emit sig_tick(actualSeconds_, true);
                emit sig_stateChanged(currentState_);
            } else {
                handleSuccess();
                return;
            }
        }
    } else {
        emit sig_tick(actualSeconds_, true);
        if (actualSeconds_ >= 7200) {
            handleSuccess();
            return;
        }
    }

    if (focusMode_ == FocusMode::GENTLE_MODE && violationCount_ > 0) {
        ++violationSeconds_;
    }

    updateGrowth();
    if (actualSeconds_ - lastCheckpointSeconds_ >= 30) {
        checkpointRecord();
    }
}

void FocusController::updateGrowth()
{
    if (!currentPlant_) return;
    int total = (currentMode_ == TimerMode::COUNTDOWN)
        ? static_cast<int>(plannedMinutes_ * 60) : 7200;
    currentPlant_->grow(static_cast<int>(actualSeconds_), total);
    const uint32_t stage = currentPlant_->getGrowthStage();
    if (stage != lastPublishedGrowthStage_) {
        lastPublishedGrowthStage_ = stage;
        emit sig_growthStageChanged(stage);
    }
}

void FocusController::handleSuccess()
{
    writeRecord(FocusRecordStatus::Success);
    uint32_t coins = calculateCoins();
    std::cout << "[FocusController] Success! Pure seconds="
              << (actualSeconds_ > violationSeconds_ ? actualSeconds_ - violationSeconds_ : 0)
              << " coins=" << coins << "\n";
}

void FocusController::handleFailure()
{
    if (currentPlant_) currentPlant_->wither();
    writeRecord(FocusRecordStatus::Failed);
}

void FocusController::writeRecord(FocusRecordStatus status)
{
    const auto existingRecord = db_.readById(recordIndex_);

    // Gentle-mode violation seconds are excluded from rewardable focus time.
    uint32_t pureSeconds = (actualSeconds_ > violationSeconds_)
        ? actualSeconds_ - violationSeconds_ : 0;

    FocusRecord record;
    record.recordId = recordIndex_;
    record.plantType = existingRecord ? existingRecord->plantType
                                      : (currentPlant_ ? currentPlant_->getPlantType() : 0);
    record.plannedMinutes = existingRecord ? existingRecord->plannedMinutes : plannedMinutes_;
    record.actualSeconds = pureSeconds;
    record.startTimestamp = existingRecord ? existingRecord->startTimestamp
                                           : currentSystemEpochSeconds();
    record.status = status;
    record.violationCount = violationCount_;
    record.growthStage = currentPlant_ ? currentPlant_->getGrowthStage() : 4;
    record.coinsEarned = (status == FocusRecordStatus::Success) ? calculateCoins() : 0;
    record.tagId = currentTagId_;
    record.focusMode = focusMode_ == FocusMode::STRICT_MODE
        ? PersistedFocusMode::Deep : PersistedFocusMode::GentleLegacy;
    record.gridIndex = existingRecord ? existingRecord->gridIndex : 0;
    if (!db_.updateById(recordIndex_, record)) {
        std::cerr << "[FOCUS SAVE ERROR] Terminal focus state was not persisted.\n";
        return;
    }
    lastCoinsEarned_ = record.coinsEarned;

    currentState_ = (status == FocusRecordStatus::Success) ? State::SUCCESS : State::FAILED;
    emit sig_stateChanged(currentState_);
    emit sig_focusFinalized(recordIndex_, static_cast<uint32_t>(status));
}

void FocusController::checkpointRecord()
{
    const auto existingRecord = db_.readById(recordIndex_);
    if (!existingRecord || currentState_ == State::IDLE) return;

    FocusRecord checkpoint = existingRecord.value();
    checkpoint.status = FocusRecordStatus::Running;
    checkpoint.actualSeconds = actualSeconds_;
    checkpoint.violationCount = violationCount_;
    checkpoint.growthStage = currentPlant_ ? currentPlant_->getGrowthStage() : checkpoint.growthStage;
    checkpoint.tagId = currentTagId_;
    if (db_.updateById(recordIndex_, checkpoint)) {
        lastCheckpointSeconds_ = actualSeconds_;
    }
}

uint32_t FocusController::calculateCoins() const
{
    // Base reward: one coin per five clean focus minutes.
    uint32_t pureSeconds = (actualSeconds_ > violationSeconds_)
        ? actualSeconds_ - violationSeconds_ : 0;
    uint32_t rawCoins = pureSeconds / 300;

    if (focusMode_ == FocusMode::GENTLE_MODE) {
        double multiplier = 1.0;
        if (violationCount_ == 0)       multiplier = 1.0;
        else if (violationCount_ <= 3)  multiplier = 0.8;
        else if (violationCount_ <= 10) multiplier = 0.5;
        else                            multiplier = 0.2;
        return static_cast<uint32_t>(rawCoins * multiplier);
    }
    return rawCoins;
}
