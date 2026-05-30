#ifndef TIMERRING_H
#define TIMERRING_H

#include <QWidget>

class TimerRing : public QWidget {
    Q_OBJECT

public:
    explicit TimerRing(QWidget* parent = nullptr);

    void setDisplaySeconds(uint32_t seconds, bool isStopwatch);
    void setQuote(const QString& quote);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    uint32_t displaySeconds_ = 0;
    bool isStopwatch_ = false;
    QString quote_;
};

#endif // TIMERRING_H
