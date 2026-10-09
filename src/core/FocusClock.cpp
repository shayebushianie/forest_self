#include "core/FocusClock.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <chrono>
#endif

qint64 FocusClock::monotonicMilliseconds()
{
#ifdef Q_OS_WIN
    // Unlike wall time, this clock cannot be changed by the user and includes sleep.
    return static_cast<qint64>(GetTickCount64());
#else
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}
