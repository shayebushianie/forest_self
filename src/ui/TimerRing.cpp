#include "ui/TimerRing.h"
#include <QPainter>
#include <QtMath>
#include <cmath>

TimerRing::TimerRing(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(200, 240);
}

void TimerRing::setDisplaySeconds(uint32_t seconds, bool isStopwatch)
{
    displaySeconds_ = seconds;
    isStopwatch_ = isStopwatch;
    update();
}

void TimerRing::setQuote(const QString& quote)
{
    quote_ = quote;
    update();
}

void TimerRing::setOath(const QString& oath)
{
    oath_ = oath;
    update();
}

void TimerRing::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int side = qMin(width(), height() - 60);
    QRectF rect((width() - side) / 2.0, 10, side, side);

    // 底色轨道
    QPen bgPen;
    bgPen.setColor(QColor(46, 63, 52, 100));
    bgPen.setWidth(14);
    bgPen.setCapStyle(Qt::RoundCap);
    painter.setPen(bgPen);
    painter.drawArc(rect, 90 * 16, -360 * 16);

    if (isStopwatch_) {
        // 正计时：呼吸起伏光晕
        static double angleOffset = 0.0;
        angleOffset += 1.5;
        if (angleOffset > 360.0) angleOffset -= 360.0;

        int alpha = static_cast<int>(177 + 78 * std::sin(angleOffset * 3.14159 / 180.0));
        QPen breathPen(QColor(78, 159, 61, alpha), 14, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(breathPen);
        painter.drawArc(rect, static_cast<int>(angleOffset * 16), 270 * 16);
    } else {
        // 倒计时：百分比进度弧
        if (displaySeconds_ > 0) {
            QPen progressPen("#4E9F3D");
            progressPen.setWidth(14);
            progressPen.setCapStyle(Qt::RoundCap);
            painter.setPen(progressPen);
            painter.drawArc(rect, 90 * 16, -360 * 16);
        }
    }

    // 树梢誓言
    if (!oath_.isEmpty()) {
        QFont oathFont("Microsoft YaHei", 12, QFont::Bold);
        painter.setFont(oathFont);
        painter.setPen(QColor("#8A9A86"));
        QRectF oathRect(0, rect.top() - 30, width(), 30);
        painter.drawText(oathRect, Qt::AlignCenter, oath_);
    }

    // 中心时间文字
    uint32_t secs = displaySeconds_;
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;

    QString timeStr;
    if (h > 0)
        timeStr = QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    else
        timeStr = QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));

    QFont timeFont("Microsoft YaHei", 30, QFont::Bold);
    painter.setFont(timeFont);
    painter.setPen(QColor("#E8EAE6"));
    painter.drawText(rect, Qt::AlignCenter, timeStr);

    // 底部自勉语录
    if (!quote_.isEmpty()) {
        QFont quoteFont("Microsoft YaHei", 11);
        painter.setFont(quoteFont);
        painter.setPen(QColor("#8A9A86"));
        QRectF quoteRect(0, side + 15, width(), 40);
        painter.drawText(quoteRect, Qt::AlignHCenter | Qt::AlignTop, quote_);
    }
}
