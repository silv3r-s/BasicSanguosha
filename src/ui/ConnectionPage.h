#pragma once

#include <QWidget>

class QLineEdit;
class QSpinBox;
class QLabel;

namespace sanguosha::ui {

class ConnectionPage final : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionPage(QWidget* parent = nullptr);
    void setStatusMessage(const QString& message);
signals:
    void createRequested(const QString& playerName, quint16 port);
    void joinRequested(const QString& host, quint16 port, const QString& playerName);
private:
    QLineEdit* hostName_ {};
    QLineEdit* joinName_ {};
    QLineEdit* address_ {};
    QSpinBox* hostPort_ {};
    QSpinBox* joinPort_ {};
    QLabel* error_ {};
};

} // namespace sanguosha::ui
