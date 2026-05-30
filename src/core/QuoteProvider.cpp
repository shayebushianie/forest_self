#include "core/QuoteProvider.h"
#include <cstdlib>
#include <ctime>

QuoteProvider::QuoteProvider(QObject* parent)
    : QObject(parent)
{
    quotes_ = {
        QStringLiteral("加油，你正在创造奇迹！"),
        QStringLiteral("专注是通往卓越的唯一道路。"),
        QStringLiteral("不要一直盯着我，快去工作吧！"),
        QStringLiteral("再坚持一下，树苗就要长大了！"),
        QStringLiteral("今天的努力，是明天的森林。"),
        QStringLiteral("每一分钟的专注，都是一片绿叶。"),
        QStringLiteral("放弃很容易，但坚持真的很酷。"),
        QStringLiteral("你和成功之间，只差一个番茄钟。"),
        QStringLiteral("种一棵树最好的时间是十年前，其次是现在。"),
        QStringLiteral("屏幕前的你，正在变得更好。"),
        QStringLiteral("深度工作一小时，胜过心不在焉一整天。"),
        QStringLiteral("不要分心，你的森林正在生长。"),
        QStringLiteral("自律给你自由。"),
        QStringLiteral("专注当下，未来自然而来。"),
        QStringLiteral("你比自己想象的更强大。"),
        QStringLiteral("每一次坚持，都在雕刻更好的自己。"),
    };
}

QString QuoteProvider::getRandomQuote() const
{
    if (quotes_.isEmpty()) return {};
    int index = std::rand() % quotes_.size();
    return quotes_[index];
}
