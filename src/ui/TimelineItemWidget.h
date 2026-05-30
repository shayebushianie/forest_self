#ifndef TIMELINEITEMWIDGET_H
#define TIMELINEITEMWIDGET_H

#include <QWidget>
#include "common/DatabaseCommon.h"

class TimelineItemWidget : public QWidget {
    Q_OBJECT

public:
    TimelineItemWidget(const FocusRecord& record, bool isFirst, bool isLast,
                       QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString formatTime(uint64_t timestamp) const;

    FocusRecord record_;
    bool isFirst_ = false;
    bool isLast_ = false;
};

#endif // TIMELINEITEMWIDGET_H
