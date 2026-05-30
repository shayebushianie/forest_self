#ifndef QUOTEPROVIDER_H
#define QUOTEPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>

class QuoteProvider : public QObject {
    Q_OBJECT

public:
    explicit QuoteProvider(QObject* parent = nullptr);

    QString getRandomQuote() const;
    int count() const { return quotes_.size(); }

private:
    QVector<QString> quotes_;
};

#endif // QUOTEPROVIDER_H
