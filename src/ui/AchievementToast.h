#ifndef ACHIEVEMENTTOAST_H
#define ACHIEVEMENTTOAST_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>

class AchievementToast : public QWidget {

public:
    static void showToast(QWidget* parent, const QString& title, const QString& desc)
    {
        auto* toast = new AchievementToast(parent, title, desc);
        toast->show();
        toast->startAnimation();
    }

private:
    AchievementToast(QWidget* parent, const QString& title, const QString& desc)
        : QWidget(parent)
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::SubWindow);
        setAttribute(Qt::WA_DeleteOnClose);
        setStyleSheet(
            "QWidget { background-color: rgba(30,41,34,230);"
            "  border: 2px solid #D8B257; border-radius: 10px; }"
            "QLabel { background:transparent; border:none; }");

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(15, 12, 15, 12);

        auto* titleLabel = new QLabel(
            QStringLiteral("解锁成就：%1").arg(title), this);
        titleLabel->setStyleSheet("font-weight:bold; font-size:16px; color:#D8B257;");
        layout->addWidget(titleLabel);

        auto* descLabel = new QLabel(desc, this);
        descLabel->setStyleSheet("font-size:13px; color:#E8EAE6;");
        layout->addWidget(descLabel);

        setFixedSize(340, 90);
        if (parent) {
            int x = (parent->width() - width()) / 2;
            move(x, 40);
        }
    }

    void startAnimation()
    {
        auto* opacityEffect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(opacityEffect);

        auto* anim = new QPropertyAnimation(opacityEffect, "opacity", this);
        anim->setDuration(1000);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(QEasingCurve::OutQuad);

        QTimer::singleShot(2500, this, [this, anim]() {
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        });

        QObject::connect(anim, &QPropertyAnimation::finished, this, &QWidget::close);
    }
};

#endif // ACHIEVEMENTTOAST_H
