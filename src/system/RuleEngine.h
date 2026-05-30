#ifndef RULEENGINE_H
#define RULEENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>

class RuleEngine : public QObject {
    Q_OBJECT

public:
    explicit RuleEngine(QObject* parent = nullptr);

    void setBlacklist(const QStringList& blacklist);
    void setWhitelist(const QStringList& whitelist);
    void addToBlacklist(const QString& processName);
    void removeFromBlacklist(const QString& processName);

    QStringList blacklist() const { return blacklist_; }
    QStringList whitelist() const { return whitelist_; }

    bool isViolation(const QString& processName) const;

private:
    QStringList blacklist_;
    QStringList whitelist_;
};

#endif // RULEENGINE_H
