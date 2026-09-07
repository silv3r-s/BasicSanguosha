#include "ui/ConnectionPage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace sanguosha::ui {

ConnectionPage::ConnectionPage(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(tr("基础三国杀"), this);
    title->setStyleSheet("font-size:24px;font-weight:600;");
    layout->addWidget(title);
    layout->addWidget(new QLabel(tr("请选择创建房间或加入已有房间。未选择前不会启动服务器。"), this));

    auto* createBox = new QGroupBox(tr("创建房间（房主）"), this);
    auto* createForm = new QFormLayout(createBox);
    hostName_ = new QLineEdit(tr("玩家1"), createBox);
    hostPort_ = new QSpinBox(createBox); hostPort_->setRange(1, 65535); hostPort_->setValue(9527);
    auto* create = new QPushButton(tr("创建房间"), createBox);
    createForm->addRow(tr("玩家昵称："), hostName_);
    createForm->addRow(tr("监听端口："), hostPort_);
    createForm->addRow(create);
    layout->addWidget(createBox);

    auto* joinBox = new QGroupBox(tr("加入房间（客户端）"), this);
    auto* joinForm = new QFormLayout(joinBox);
    address_ = new QLineEdit(QStringLiteral("127.0.0.1"), joinBox);
    joinPort_ = new QSpinBox(joinBox); joinPort_->setRange(1, 65535); joinPort_->setValue(9527);
    joinName_ = new QLineEdit(tr("玩家2"), joinBox);
    auto* join = new QPushButton(tr("加入房间"), joinBox);
    joinForm->addRow(tr("服务器地址："), address_);
    joinForm->addRow(tr("端口："), joinPort_);
    joinForm->addRow(tr("玩家昵称："), joinName_);
    joinForm->addRow(join);
    layout->addWidget(joinBox);
    error_ = new QLabel(this); error_->setStyleSheet("color:#9b2020;"); error_->setWordWrap(true); layout->addWidget(error_);
    layout->addStretch();
    connect(create, &QPushButton::clicked, this, [this] { emit createRequested(hostName_->text().trimmed(), quint16(hostPort_->value())); });
    connect(join, &QPushButton::clicked, this, [this] {
        if (address_->text().trimmed().isEmpty()) { error_->setText(tr("请输入服务器地址。")); return; }
        emit joinRequested(address_->text().trimmed(), quint16(joinPort_->value()), joinName_->text().trimmed());
    });
}

void ConnectionPage::setStatusMessage(const QString& message)
{
    error_->setText(message);
}

} // namespace sanguosha::ui
