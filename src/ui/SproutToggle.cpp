#include "ui/SproutToggle.h"

#include "ui/AppStyle.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace {

QColor mixColor(const QColor& from, const QColor& to, qreal progress)
{
    const auto mix = [progress](int first, int second) {
        return qRound(first + (second - first) * progress);
    };
    return QColor(mix(from.red(), to.red()), mix(from.green(), to.green()),
                  mix(from.blue(), to.blue()), mix(from.alpha(), to.alpha()));
}

} // namespace

SproutToggle::SproutToggle(QWidget* parent)
    : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setToolTip(QStringLiteral("切换此设置"));
    setAccessibleDescription(QStringLiteral("显示“未启用”或“已启用”，按空格或回车切换"));

    animation_ = new QVariantAnimation(this);
    animation_->setObjectName(QStringLiteral("sproutToggleAnimation"));
    animation_->setDuration(150);
    animation_->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        visualProgress_ = value.toReal();
        update();
    });
    connect(this, &QCheckBox::toggled, this, [this](bool checked) {
        const bool animated = animateNextStateChange_;
        animateNextStateChange_ = false;
        updateVisualState(checked, animated);
    });
}

QString SproutToggle::statusText() const
{
    return isChecked() ? QStringLiteral("已启用") : QStringLiteral("未启用");
}

void SproutToggle::setReducedMotion(bool reduced)
{
    reducedMotion_ = reduced;
    if (reducedMotion_ && animation_->state() == QAbstractAnimation::Running)
        animation_->stop();
    if (reducedMotion_) {
        visualProgress_ = isChecked() ? 1.0 : 0.0;
        update();
    }
}

QSize SproutToggle::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int textWidth = qMax(metrics.horizontalAdvance(QStringLiteral("未启用")),
                               metrics.horizontalAdvance(QStringLiteral("已启用")));
    return QSize(textWidth + 58, qMax(38, metrics.height() + 18));
}

QSize SproutToggle::minimumSizeHint() const
{
    return sizeHint();
}

void SproutToggle::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        click();
        event->accept();
        return;
    }
    QCheckBox::keyPressEvent(event);
}

void SproutToggle::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled() && rect().contains(event->position().toPoint())) {
        pointerPressActive_ = true;
        setDown(true);
        emit pressed();
        event->accept();
        return;
    }
    QCheckBox::mousePressEvent(event);
}

void SproutToggle::mouseMoveEvent(QMouseEvent* event)
{
    if (pointerPressActive_) {
        setDown(rect().contains(event->position().toPoint()));
        event->accept();
        return;
    }
    QCheckBox::mouseMoveEvent(event);
}

void SproutToggle::mouseReleaseEvent(QMouseEvent* event)
{
    if (pointerPressActive_ && event->button() == Qt::LeftButton) {
        const bool activate = rect().contains(event->position().toPoint());
        pointerPressActive_ = false;
        setDown(false);
        emit released();
        if (activate) {
            animateNextStateChange_ = true;
            setChecked(!isChecked());
            emit clicked(isChecked());
        }
        event->accept();
        return;
    }
    pointerPressActive_ = false;
    setDown(false);
    QCheckBox::mouseReleaseEvent(event);
}

void SproutToggle::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF surface = rect().adjusted(2, 2, -2, -2);
    if (isDown()) surface.translate(0, 1);

    QColor offBackground("#E4ECDD");
    QColor onBackground("#EAF3D0");
    QColor offBorder("#93A68D");
    QColor onBorder("#86A95F");
    QColor offText("#556A59");
    QColor onText("#38643A");
    QColor background = mixColor(offBackground, onBackground, visualProgress_);
    QColor border = mixColor(offBorder, onBorder, visualProgress_);
    QColor textColor = mixColor(offText, onText, visualProgress_);
    if (underMouse() && isEnabled()) background = background.lighter(104);
    if (isDown() && isEnabled()) background = background.darker(104);
    if (!isEnabled()) {
        background.setAlpha(145);
        border.setAlpha(130);
        textColor.setAlpha(145);
    }

    painter.setPen(QPen(border, 1.2));
    painter.setBrush(background);
    painter.drawRoundedRect(surface, 11, 11);

    const qreal badgeSize = surface.height() - 10;
    const QRectF badge(surface.left() + 5, surface.top() + 5, badgeSize, badgeSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mixColor(QColor("#91A38C"), QColor("#70A95F"), visualProgress_));
    painter.drawEllipse(badge);

    painter.save();
    painter.setClipRect(badge);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(238, 247, 232, qRound(255 * (1.0 - visualProgress_))));
    painter.drawEllipse(badge.center(), 3.2, 3.2);
    painter.restore();

    painter.save();
    painter.setOpacity(visualProgress_);
    QPen stemPen(QColor("#F5FFF1"), 1.7, Qt::SolidLine, Qt::RoundCap);
    painter.setPen(stemPen);
    const QPointF stemBottom(badge.center().x(), badge.bottom() - 6);
    const QPointF stemTop(badge.center().x(), badge.top() + 7);
    painter.drawLine(stemBottom, stemTop);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#F5FFF1"));
    painter.drawEllipse(QRectF(stemTop.x() - 6, stemTop.y() - 1, 7, 4));
    painter.drawEllipse(QRectF(stemTop.x(), stemTop.y() + 2, 7, 4));
    painter.restore();

    painter.setPen(textColor);
    const QRectF textRect(badge.right() + 8, surface.top(), surface.right() - badge.right() - 13,
                          surface.height());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, statusText());

    if (AppStyle::keyboardFocusVisible(this)) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#D4B844"));
        painter.drawEllipse(QPointF(surface.right() - 8, surface.top() + 8), 3.5, 3.5);
    }
}

void SproutToggle::updateVisualState(bool checked, bool animated)
{
    const qreal target = checked ? 1.0 : 0.0;
    if (!animated || reducedMotion_ || !isVisible()) {
        animation_->stop();
        visualProgress_ = target;
        update();
        return;
    }

    animation_->stop();
    animation_->setStartValue(visualProgress_);
    animation_->setEndValue(target);
    animation_->start();
}
