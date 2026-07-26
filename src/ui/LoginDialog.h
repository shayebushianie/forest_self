#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include "storage/UserManager.h"

class LoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LoginDialog(UserManager& userManager, QWidget* parent = nullptr);

    uint32_t getLoggedInUserId() const { return loggedUserId_; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onLoginClicked();
    void onRegisterClicked();

private:
    UserManager& userManager_;
    uint32_t loggedUserId_ = 0;

    QLineEdit* usernameEdit_;
    QLineEdit* passwordEdit_;
    QLabel* statusLabel_;
};

#endif // LOGINDIALOG_H
