#include "system/RuleEngine.h"

RuleEngine::RuleEngine(QObject* parent)
    : QObject(parent)
{
}

void RuleEngine::setBlacklist(const QStringList& blacklist)
{
    blacklist_ = blacklist;
}

void RuleEngine::setWhitelist(const QStringList& whitelist)
{
    whitelist_ = whitelist;
}

void RuleEngine::addToBlacklist(const QString& processName)
{
    if (!blacklist_.contains(processName)) {
        blacklist_.append(processName);
    }
}

void RuleEngine::removeFromBlacklist(const QString& processName)
{
    blacklist_.removeAll(processName);
}

bool RuleEngine::isViolation(const QString& processName) const
{
    if (processName.isEmpty()) return false;

    for (const QString& blocked : blacklist_) {
        if (QString::compare(blocked, processName, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
