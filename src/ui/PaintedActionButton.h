#ifndef PAINTEDACTIONBUTTON_H
#define PAINTEDACTIONBUTTON_H

#include <QKeyEvent>
#include <QToolButton>

// Transparent semantic action for controls rendered by a parent painter.
class PaintedActionButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            event->accept();
            return;
        }
        QToolButton::keyPressEvent(event);
    }
};

#endif // PAINTEDACTIONBUTTON_H
