#ifndef DIALOG_PRESENTER_H
#define DIALOG_PRESENTER_H

#include <QMessageBox>
#include <QAbstractButton>
#include <QDialog>
#include <QPushButton>
#include <QWidget>

class DialogPresenter final {
public:
    static void information(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static void warning(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Warning, title, message, QMessageBox::Ok);
    }

    static void error(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Critical, title, message, QMessageBox::Ok);
    }

    static void detail(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static void empty(QWidget* parent, const QString& title, const QString& message)
    {
        show(parent, QMessageBox::Information, title, message, QMessageBox::Ok);
    }

    static bool confirm(QWidget* parent, const QString& title, const QString& message)
    {
        return show(parent, QMessageBox::Question, title, message,
                    QMessageBox::No | QMessageBox::Yes, QMessageBox::No) == QMessageBox::Yes;
    }

    static bool confirmDanger(QWidget* parent, const QString& title, const QString& message)
    {
        return show(parent, QMessageBox::Warning, title, message,
                    QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel, true) == QMessageBox::Yes;
    }

    static bool detailWithAction(QWidget* parent, const QString& title, const QString& message,
                                 const QString& actionText)
    {
        QMessageBox dialog(parent);
        dialog.setIcon(QMessageBox::Information);
        dialog.setWindowTitle(title);
        dialog.setText(message);
        prepare(dialog);
        QAbstractButton* action = dialog.addButton(actionText, QMessageBox::AcceptRole);
        dialog.addButton(QStringLiteral("关闭"), QMessageBox::RejectRole);
        applyButtonRoles(dialog, action);
        dialog.exec();
        return dialog.clickedButton() == action;
    }

    static void prepare(QMessageBox& dialog)
    {
        dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground, true);
        dialog.setObjectName("forestDialog");
        dialog.setModal(true);
    }

    static void prepare(QDialog& dialog)
    {
        dialog.setWindowFlags(dialog.windowFlags() | Qt::FramelessWindowHint);
        dialog.setAttribute(Qt::WA_StyledBackground, true);
        dialog.setObjectName("forestDialog");
        dialog.setModal(true);
    }

    static void setPrimary(QPushButton* button) { if (button) button->setObjectName("btnPrimary"); }
    static void setSecondary(QPushButton* button) { if (button) button->setObjectName("btnSecondary"); }
    static void setDanger(QPushButton* button) { if (button) button->setObjectName("btnDanger"); }

private:
    static QMessageBox::StandardButton show(QWidget* parent, QMessageBox::Icon icon,
                                            const QString& title, const QString& message,
                                            QMessageBox::StandardButtons buttons,
                                            QMessageBox::StandardButton defaultButton = QMessageBox::Ok,
                                            bool destructive = false)
    {
        QMessageBox dialog(icon, title, message, buttons, parent);
        prepare(dialog);
        dialog.setDefaultButton(defaultButton);
        applyButtonRoles(dialog, nullptr, destructive);
        return static_cast<QMessageBox::StandardButton>(dialog.exec());
    }

    static void applyButtonRoles(QMessageBox& dialog, QAbstractButton* primaryAction = nullptr,
                                 bool destructive = false)
    {
        for (QAbstractButton* button : dialog.buttons()) {
            if (button == dialog.button(QMessageBox::Yes) && destructive) button->setObjectName("btnDanger");
            else if (button == dialog.button(QMessageBox::Yes) || button == dialog.button(QMessageBox::Ok) || button == primaryAction) button->setObjectName("btnPrimary");
            else button->setObjectName("btnSecondary");
        }
    }
};

#endif // DIALOG_PRESENTER_H
