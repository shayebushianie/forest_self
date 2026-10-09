#ifndef TESTFOCUSCLOCK_H
#define TESTFOCUSCLOCK_H

#include "core/FocusController.h"
#include <QDateTime>

struct TestFocusClock {
    qint64 milliseconds = 0;
    uint64_t epoch = static_cast<uint64_t>(QDateTime::currentSecsSinceEpoch());

    FocusController::Clock clock() {
        return {[this]() { return milliseconds; }, [this]() { return epoch; }};
    }
    void advanceSeconds(qint64 seconds) { milliseconds += seconds * 1000; }
};

#endif
