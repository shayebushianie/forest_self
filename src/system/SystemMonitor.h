#ifndef SYSTEMMONITOR_H
#define SYSTEMMONITOR_H

#include <QObject>
#include <QTimer>

class RuleEngine;

// Polls the foreground Windows process and emits edge-triggered signals when
// the user enters or leaves a blacklisted app.
class SystemMonitor : public QObject {
    Q_OBJECT

public:
    explicit SystemMonitor(RuleEngine& ruleEngine, QObject* parent = nullptr);

    void startMonitoring();
    void stopMonitoring();
    bool isMonitoring() const { return monitoring_; }

signals:
    void sig_violationDetected(const QString& processName);
    void sig_safeWindowDetected();

private slots:
    void checkForegroundWindow();

private:
    RuleEngine& ruleEngine_;
    QTimer timer_;
    bool monitoring_ = false;
    bool wasViolating_ = false;
};

#endif // SYSTEMMONITOR_H
