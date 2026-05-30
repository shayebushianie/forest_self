#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include "system/SystemMonitor.h"
#include "system/RuleEngine.h"
#include <QString>
#include <iostream>

SystemMonitor::SystemMonitor(RuleEngine& ruleEngine, QObject* parent)
    : QObject(parent), ruleEngine_(ruleEngine)
{
    QObject::connect(&timer_, &QTimer::timeout,
                     this, &SystemMonitor::checkForegroundWindow);
}

void SystemMonitor::startMonitoring()
{
    if (monitoring_) return;
    monitoring_ = true;
    timer_.start(500);
    std::cout << "[SystemMonitor] Monitoring started (500ms interval).\n";
}

void SystemMonitor::stopMonitoring()
{
    if (!monitoring_) return;
    timer_.stop();
    monitoring_ = false;
    std::cout << "[SystemMonitor] Monitoring stopped.\n";
}

void SystemMonitor::checkForegroundWindow()
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess) return;

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
        std::wstring fullPath(path);
        size_t lastSlash = fullPath.find_last_of(L"\\");
        std::wstring exeName = (lastSlash == std::wstring::npos)
            ? fullPath : fullPath.substr(lastSlash + 1);

        QString qExeName = QString::fromStdWString(exeName);
        if (ruleEngine_.isViolation(qExeName)) {
            if (!wasViolating_) {
                emit sig_violationDetected(qExeName);
            }
            wasViolating_ = true;
        } else {
            if (wasViolating_) {
                emit sig_safeWindowDetected();
            }
            wasViolating_ = false;
        }
    }

    CloseHandle(hProcess);
}
