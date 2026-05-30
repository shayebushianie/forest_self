#include "ui/LoginDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>

LoginDialog::LoginDialog(UserManager& userManager, QWidget* parent)
    : QDialog(parent), userManager_(userManager)
{
    setWindowTitle(QStringLiteral("Forest — 用户登录"));
    setFixedSize(360, 280);
    setObjectName("LoginDialog");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(32, 24, 32, 24);
    layout->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("登录 Forest"), this);
    title->setStyleSheet("font-size:20px; font-weight:bold; color:#4E9F3D; background:transparent; border:none;");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    layout->addSpacing(8);

    auto* userLabel = new QLabel(QStringLiteral("用户名"), this);
    userLabel->setStyleSheet("color:#8A9A86; background:transparent; border:none; font-size:12px;");
    layout->addWidget(userLabel);

    usernameEdit_ = new QLineEdit(this);
    usernameEdit_->setPlaceholderText(QStringLiteral("输入用户名"));
    layout->addWidget(usernameEdit_);

    auto* passLabel = new QLabel(QStringLiteral("密码"), this);
    passLabel->setStyleSheet("color:#8A9A86; background:transparent; border:none; font-size:12px;");
    layout->addWidget(passLabel);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setPlaceholderText(QStringLiteral("输入密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    layout->addWidget(passwordEdit_);

    auto* btnLayout = new QHBoxLayout;
    auto* loginBtn = new QPushButton(QStringLiteral("登录"), this);
    loginBtn->setObjectName("btnPrimary");
    auto* registerBtn = new QPushButton(QStringLiteral("注册"), this);
    btnLayout->addWidget(loginBtn);
    btnLayout->addWidget(registerBtn);
    layout->addLayout(btnLayout);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet("color:#8A9A86; background:transparent; border:none; font-size:12px;");
    layout->addWidget(statusLabel_);

    QObject::connect(loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    QObject::connect(registerBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    QObject::connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginDialog::onLoginClicked);
}

void LoginDialog::onLoginClicked()
{
    std::string user = usernameEdit_->text().toStdString();
    std::string pass = passwordEdit_->text().toStdString();

    uint32_t uid = 0;
    if (userManager_.loginUser(user, pass, uid)) {
        loggedUserId_ = uid;
        accept();
    } else {
        statusLabel_->setText(QStringLiteral("用户名或密码错误"));
        statusLabel_->setStyleSheet("color:#C74B4B; background:transparent; border:none; font-size:12px;");
    }
}

void LoginDialog::onRegisterClicked()
{
    std::string user = usernameEdit_->text().toStdString();
    std::string pass = passwordEdit_->text().toStdString();

    if (userManager_.registerUser(user, pass)) {
        statusLabel_->setText(QStringLiteral("注册成功！请点击登录"));
        statusLabel_->setStyleSheet("color:#4E9F3D; background:transparent; border:none; font-size:12px;");
    } else {
        statusLabel_->setText(QStringLiteral("注册失败（用户名已存在或格式不正确）"));
        statusLabel_->setStyleSheet("color:#C74B4B; background:transparent; border:none; font-size:12px;");
    }
}
