#ifndef FOCUS_RECORD_SEMANTICS_H
#define FOCUS_RECORD_SEMANTICS_H

#include "common/DatabaseCommon.h"
#include <QDateTime>
#include <QString>
#include <algorithm>
#include <limits>

namespace FocusRecordSemantics {
inline QDate startDate(const FocusRecord& record)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(record.startTimestamp), Qt::LocalTime).date();
}

inline int wholeMinutes(uint64_t seconds)
{
    return static_cast<int>(std::min<uint64_t>(seconds / 60, std::numeric_limits<int>::max()));
}

inline QString statusText(const FocusRecord& record)
{
    if (isInterruptedFocus(record)) return QStringLiteral("中断恢复");
    switch (record.status) {
    case FocusRecordStatus::Success: return QStringLiteral("成功完成");
    case FocusRecordStatus::Failed: return QStringLiteral("规则失败");
    case FocusRecordStatus::Abandoned: return QStringLiteral("放弃");
    case FocusRecordStatus::Running: return QStringLiteral("进行中");
    }
    return QStringLiteral("未知状态");
}
}
#endif
