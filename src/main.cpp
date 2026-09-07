#include <memory>

#include <QApplication>
#include <QMessageBox>
#include <QStackedWidget>
#include <QTimer>

#include "client/GameClientController.h"
#include "network/GameServer.h"
#include "network/NetworkGameClient.h"
#include "ui/ConnectionPage.h"
#include "ui/MainWindow.h"

namespace {

class ApplicationController final : public QObject {
public:
    explicit ApplicationController(QApplication& application) : application_(application)
    {
        pages_.setWindowTitle(QObject::tr("基础三国杀"));
        pages_.resize(1280, 820);
        pages_.setMinimumSize(960, 700);
        pages_.addWidget(&connectionPage_);
        connect(&connectionPage_, &sanguosha::ui::ConnectionPage::createRequested, this,
            [this](const QString& name, quint16 port) { createHost(name, port); });
        connect(&connectionPage_, &sanguosha::ui::ConnectionPage::joinRequested, this,
            [this](const QString& host, quint16 port, const QString& name) { joinHost(host, port, name); });
    }

    void showConnectionPage() { connectionPage_.setStatusMessage(lastConnectionError_); pages_.setCurrentWidget(&connectionPage_); pages_.show(); }

    bool createHost(const QString& name, quint16 port)
    {
        clearSession();
        auto server = std::make_unique<sanguosha::network::GameServer>();
        server->setHostDisplayName(name);
        QString error;
        if (!server->listen(error, port)) { QMessageBox::critical(&pages_, QObject::tr("无法创建房间"), error); return false; }
        auto client = std::make_unique<sanguosha::GameClientController>(server->session(), 1);
        client->setConnectionStatus(QStringLiteral("房主已监听 127.0.0.1:%1，等待玩家加入。").arg(port).toStdString(), true);
        attachHost(std::move(server), std::move(client));
        return true;
    }

    void joinHost(const QString& host, quint16 port, const QString& name)
    {
        lastConnectionError_.clear();
        connectionPage_.setStatusMessage({});
        clearSession();
        auto client = std::make_unique<sanguosha::network::NetworkGameClient>(host, port, name);
        auto* remote = client.get();
        attachRemote(std::move(client));
        remote->connectToHost();
    }

private:
    void attachHost(std::unique_ptr<sanguosha::network::GameServer> server, std::unique_ptr<sanguosha::GameClientController> client)
    {
        server_ = std::move(server);
        client_ = std::move(client);
        sessionWindow_ = std::make_unique<sanguosha::ui::MainWindow>(*client_, server_.get(), [this] { scheduleLeave(); });
        pages_.addWidget(sessionWindow_.get());
        QObject::connect(server_.get(), &sanguosha::network::GameServer::connectionStatusChanged, sessionWindow_.get(),
            [this](const QString& status, bool connected) { static_cast<sanguosha::GameClientController*>(client_.get())->setConnectionStatus(status.toStdString(), connected); });
        QObject::connect(server_.get(), &sanguosha::network::GameServer::stateChanged, sessionWindow_.get(),
            [this] { client_->refresh(); });
        auto* timer = new QTimer(sessionWindow_.get());
        QObject::connect(timer, &QTimer::timeout, sessionWindow_.get(), [this] { if (server_ && server_->remoteConnected()) server_->broadcastViews(); });
        timer->start(100);
        pages_.setCurrentWidget(sessionWindow_.get());
    }

    void attachRemote(std::unique_ptr<sanguosha::network::NetworkGameClient> client)
    {
        client_ = std::move(client);
        sessionWindow_ = std::make_unique<sanguosha::ui::MainWindow>(*client_, nullptr, [this] { scheduleLeave(); });
        pages_.addWidget(sessionWindow_.get());
        const auto* remote = static_cast<sanguosha::network::NetworkGameClient*>(client_.get());
        QObject::connect(const_cast<sanguosha::network::NetworkGameClient*>(remote), &sanguosha::network::NetworkGameClient::statusChanged, sessionWindow_.get(), [this] {
            const auto* current = dynamic_cast<const sanguosha::network::NetworkGameClient*>(client_.get());
            if (!current) return;
            if (current->connectionState() == sanguosha::network::ConnectionState::ConnectionLost) {
                lastConnectionError_ = QObject::tr("与房主的连接已断开。请检查网络，或确认房主仍在房间中。");
                scheduleLeave();
            } else if (current->connectionState() == sanguosha::network::ConnectionState::Error) {
                lastConnectionError_ = QObject::tr("无法连接房主。请检查局域网 IPv4 地址、端口、房主状态和协议版本。");
                scheduleLeave();
            }
        });
        pages_.setCurrentWidget(sessionWindow_.get());
    }

    void scheduleLeave()
    {
        if (leaving_) return;
        leaving_ = true;
        QTimer::singleShot(0, this, [this] { clearSession(); showConnectionPage(); leaving_ = false; });
    }

    void clearSession()
    {
        if (auto* remote = dynamic_cast<sanguosha::network::NetworkGameClient*>(client_.get())) remote->disconnectFromHost();
        if (server_) server_->close();
        if (sessionWindow_) { pages_.removeWidget(sessionWindow_.get()); sessionWindow_.reset(); }
        client_.reset();
        server_.reset();
    }

    QApplication& application_;
    QStackedWidget pages_;
    sanguosha::ui::ConnectionPage connectionPage_;
    std::unique_ptr<sanguosha::network::GameServer> server_;
    std::unique_ptr<sanguosha::IGameClient> client_;
    std::unique_ptr<sanguosha::ui::MainWindow> sessionWindow_;
    QString lastConnectionError_;
    bool leaving_ {false};
};

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    const auto arguments = application.arguments();
    const bool host = arguments.contains(QStringLiteral("--host"));
    int joinIndex = arguments.indexOf(QStringLiteral("--connect"));
    if (joinIndex < 0) joinIndex = arguments.indexOf(QStringLiteral("--join"));
    const int portIndex = arguments.indexOf(QStringLiteral("--port"));
    const quint16 port = portIndex >= 0 && portIndex + 1 < arguments.size() ? quint16(arguments.at(portIndex + 1).toUShort()) : 9527;
    const int nameIndex = arguments.indexOf(QStringLiteral("--name"));
    const QString name = nameIndex >= 0 && nameIndex + 1 < arguments.size() ? arguments.at(nameIndex + 1) : QString();
    if (host && joinIndex >= 0) { QMessageBox::critical(nullptr, QObject::tr("启动参数错误"), QObject::tr("请使用 --host 或 --connect <地址>。")); return 1; }

    ApplicationController controller(application);
    if (host) controller.createHost(name, port);
    else if (joinIndex >= 0 && joinIndex + 1 < arguments.size()) controller.joinHost(arguments.at(joinIndex + 1), port, name);
    else controller.showConnectionPage();
    return application.exec();
}
