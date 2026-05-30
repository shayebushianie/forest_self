#include "ui/TimelineItemWidget.h"
#include <QPainter>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>

TimelineItemWidget::TimelineItemWidget(const FocusRecord& record,
                                        bool isFirst, bool isLast,
                                        QWidget* parent)
    : QWidget(parent), record_(record), isFirst_(isFirst), isLast_(isLast)
{
    setFixedHeight(56);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(55, 0, 15, 0);

    auto* timeLabel = new QLabel(this);
    uint32_t mins = (record_.actualSeconds + 59) / 60;
    timeLabel->setText(QStringLiteral("%1  (%2 min)").arg(formatTime(record_.startTimestamp)).arg(mins));
    timeLabel->setStyleSheet("color:#E8EAE6; font-weight:bold; font-size:12px;");
    layout->addWidget(timeLabel);

    auto* plantLabel = new QLabel(this);
    if (record_.status == 0) {
        const char* names[] = {"橡树", "松树", "玫瑰"};
        plantLabel->setText(QString::fromUtf8(names[record_.plantType % 3]));
        plantLabel->setStyleSheet("color:#4E9F3D; font-size:12px;");
    } else {
        plantLabel->setText(QStringLiteral("枯萎"));
        plantLabel->setStyleSheet("color:#8A9A86; font-size:12px;");
    }
    layout->addWidget(plantLabel);

    const char* tags[] = {"无", "学习", "写代码", "阅读", "运动"};
    auto* tagLabel = new QLabel(
        QStringLiteral("#%1").arg(QString::fromUtf8(tags[record_.tagId % 5])), this);
    tagLabel->setStyleSheet("color:#8A9A86; font-size:11px;");
    layout->addWidget(tagLabel);
    layout->addStretch();

    auto* statusLabel = new QLabel(this);
    if (record_.status == 0) {
        statusLabel->setText(QStringLiteral("+%1").arg(record_.coinsEarned));
        statusLabel->setStyleSheet("color:#D8B257; font-weight:bold; font-size:13px;");
    } else if (record_.status == 1) {
        statusLabel->setText(QStringLiteral("违规"));
        statusLabel->setStyleSheet("color:#C74B4B; font-size:12px;");
    } else {
        statusLabel->setText(QStringLiteral("放弃"));
        statusLabel->setStyleSheet("color:#8A9A86; font-size:12px;");
    }
    layout->addWidget(statusLabel);
}

void TimelineItemWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    int midY = height() / 2;
    int dotX = 28;

    QPen linePen(QColor("#2e3f34"), 2);
    painter.setPen(linePen);

    if (isFirst_ && isLast_) {
        // 单条记录不画线
    } else if (isFirst_) {
        painter.drawLine(dotX, midY, dotX, height());
    } else if (isLast_) {
        painter.drawLine(dotX, 0, dotX, midY);
    } else {
        painter.drawLine(dotX, 0, dotX, height());
    }

    QColor dotColor;
    if (record_.status == 0)      dotColor = QColor("#4E9F3D");
    else if (record_.status == 1) dotColor = QColor("#C74B4B");
    else                          dotColor = QColor("#D8B257");

    painter.setBrush(dotColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPoint(dotX, midY), 6, 6);
}

QString TimelineItemWidget::formatTime(uint64_t timestamp) const
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(timestamp))
        .toString("MM-dd hh:mm");
}
