#include "ui/LoginDialog.h"
#include "ui/DialogPresenter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>

LoginDialog::LoginDialog(UserManager& userManager, QWidget* parent)
    : QDialog(parent), userManager_(userManager)
{
    DialogPresenter::prepare(*this);
    setWindowTitle(QStringLiteral("forest — 用户登录"));
    setFixedSize(380, 318);
    setObjectName("LoginDialog");
    setStyleSheet(
        "QDialog#LoginDialog { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        " stop:0 #EAF7CB, stop:1 #BDE8C8); border:2px solid #3F9670; border-radius:24px; }"
        "QLabel#loginForestTitle { color:#245543; font-size:22px; font-weight:900; }"
        "QLineEdit { background:#FFFDEB; border:2px solid #8FC486; border-radius:12px;"
        " color:#245543; padding:5px 10px; font-size:14px; }"
        "QLineEdit:focus { background:#FFFFFF; border-color:#3E9A6F; }");

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(34, 28, 34, 26);
    layout->setSpacing(12);

    auto* title = new QLabel(QString::fromUtf8(u8"🌿 登录 Forest"), this);
    title->setObjectName("loginForestTitle");
    title->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);

    layout->addSpacing(8);

    auto* userLabel = new QLabel(QStringLiteral("用户名"), this);
    userLabel->setStyleSheet("color:#315747; background:transparent; border:none; font-size:12px; font-weight:700;");
    layout->addWidget(userLabel);

    usernameEdit_ = new QLineEdit(this);
    usernameEdit_->setFixedHeight(38);
    usernameEdit_->setPlaceholderText(QStringLiteral("输入用户名"));
    layout->addWidget(usernameEdit_);

    auto* passLabel = new QLabel(QStringLiteral("密码"), this);
    passLabel->setStyleSheet("color:#315747; background:transparent; border:none; font-size:12px; font-weight:700;");
    layout->addWidget(passLabel);

    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setFixedHeight(38);
    passwordEdit_->setPlaceholderText(QStringLiteral("输入密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    layout->addWidget(passwordEdit_);

    auto* btnLayout = new QHBoxLayout;
    auto* loginBtn = new QPushButton(QStringLiteral("登录"), this);
    loginBtn->setObjectName("btnPrimary");
    auto* registerBtn = new QPushButton(QStringLiteral("注册"), this);
    registerBtn->setObjectName("btnSecondary");
    btnLayout->addWidget(loginBtn);
    btnLayout->addWidget(registerBtn);
    layout->addLayout(btnLayout);

    statusLabel_ = new QLabel(this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet("color:#315747; background:transparent; border:none; font-size:12px;");
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
        statusLabel_->setStyleSheet("color:#2F7D44; background:transparent; border:none; font-size:12px; font-weight:700;");
    } else {
        statusLabel_->setText(QStringLiteral("注册失败（用户名已存在或格式不正确）"));
        statusLabel_->setStyleSheet("color:#C74B4B; background:transparent; border:none; font-size:12px;");
    }
}
