#ifndef RESTORETRANSACTION_H
#define RESTORETRANSACTION_H

#include <QString>

// Runs only at startup, while the data-directory lock is held and stores are closed.
class RestoreTransaction final {
public:
    struct Fault {
        enum class Point { None, ApplyFailure, ApplyInterruption, RollbackInterruption, AfterCommit };
        Point point = Point::None;
        int afterFiles = 1;
    };
    static bool recover(const QString& dataDirectory, QString* restoredFrom, QString* error,
                        QString* recoveryNotice = nullptr);
    static bool apply(const QString& dataDirectory, const QString& backupDirectory,
                      QString* restoredFrom, QString* error, const Fault* fault = nullptr);
};

#endif
