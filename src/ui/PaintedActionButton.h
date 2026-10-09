#ifndef PAINTED_ACTION_BUTTON_H
#define PAINTED_ACTION_BUTTON_H

#include <QKeyEvent>
#include <QScrollArea>
#include <QToolButton>
#include <functional>

// Native keyboard/accessibility target for an action already painted by its page.
class PaintedActionButton final : public QToolButton {
public:
    explicit PaintedActionButton(QWidget* parent) : QToolButton(parent)
    {
        setFocusPolicy(Qt::StrongFocus);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet("QToolButton { background:transparent; border:2px solid transparent; border-radius:6px; }"
                     "QToolButton:focus { border:2px solid #173F33; background:rgba(255,255,255,30); }");
        connect(this, &QToolButton::clicked, this, [this] { if (action_) action_(); });
    }

    void bind(const QRectF& area, const QString& name, std::function<void()> action)
    {
        setGeometry(area.toAlignedRect());
        setAccessibleName(name);
        setToolTip(name);
        action_ = std::move(action);
        setVisible(!area.isEmpty());
    }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (!event->isAutoRepeat()) click();
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }

    void focusInEvent(QFocusEvent* event) override
    {
        QToolButton::focusInEvent(event);
        for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
            if (auto* scroll = qobject_cast<QScrollArea*>(ancestor)) {
                scroll->ensureWidgetVisible(this, 12, 12);
                break;
            }
        }
    }

private:
    std::function<void()> action_;
};

#endif // PAINTED_ACTION_BUTTON_H
