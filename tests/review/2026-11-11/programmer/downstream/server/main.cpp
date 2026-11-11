// Programmer-review downstream: serve MyDevice over the SDK TCP reference profile.
// The SDK supplies integration::tcp::Server (framing + dispatch) but no socket, so
// the QTcpServer glue below is application code. Localhost only. MIT.
#include "../device/MyDevice.hpp"
// Order matters: EncodedProfile.hpp is not self-contained (needs tcp::Input/Output from
// Server.hpp). The order shown in doc/QtConnectionSession.md:201 fails to compile.
#include <integration/tcp/Server.hpp>
#include <integration/tcp/EncodedProfile.hpp>
#include <telemetry/Telemetry.hpp>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>
#include <array>
#include <cstdio>
#include <memory>

namespace tcp = telemetry::integration::tcp;

class Connection {
public:
    explicit Connection(QTcpSocket* socket) : socket_(socket)
    {
        profile_ = std::make_unique<tcp::EncodedProfile>(mydevice::descriptor(), mydevice::fingerprint());
        server_ = std::make_unique<tcp::Server>(mydevice::model(), mydevice::workspace(), *profile_, rx_, tx_);
    }
    ~Connection() { if (server_) server_->close(); }
    // Returns false when the connection must be dropped.
    bool onReadyRead()
    {
        pending_ += socket_->readAll();
        if (pending_.size() > 16384)
            return false;
        while (!pending_.isEmpty()) {
            const auto result = server_->receive({reinterpret_cast<const std::byte*>(pending_.constData()),
                                                  static_cast<std::size_t>(pending_.size())});
            pending_.remove(0, static_cast<qsizetype>(result.consumed));
            if (result.status == tcp::Status::Ready) {
                const auto reply = server_->reply();
                socket_->write(reinterpret_cast<const char*>(reply.data()), static_cast<qint64>(reply.size()));
                server_->releaseReply(); // QTcpSocket owns a copy now
                ++requests;
            } else if (result.status == tcp::Status::NeedMore && result.consumed != 0) {
                continue;
            } else if (result.status == tcp::Status::NeedMore) {
                break;
            } else {
                std::fprintf(stderr, "server: closing on status %d\n", int(result.status));
                return false;
            }
        }
        return true;
    }
    unsigned long long requests = 0;
private:
    QTcpSocket* socket_;
    std::array<std::byte, 8192> rx_{}, tx_{};
    std::unique_ptr<tcp::EncodedProfile> profile_;
    std::unique_ptr<tcp::Server> server_;
    QByteArray pending_;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser args;
    args.addOption({QStringLiteral("port"), QStringLiteral("TCP port on 127.0.0.1"), QStringLiteral("port"), QStringLiteral("5591")});
    args.addOption({QStringLiteral("quit-after"), QStringLiteral("Exit after ms"), QStringLiteral("ms"), QStringLiteral("0")});
    args.process(app);

    QTcpServer listener;
    std::unique_ptr<Connection> active;
    QTcpSocket* activeSocket = nullptr;
    if (!listener.listen(QHostAddress::LocalHost, quint16(args.value(QStringLiteral("port")).toUInt()))) {
        std::fprintf(stderr, "listen failed: %s\n", qPrintable(listener.errorString()));
        return 1;
    }
    std::printf("{\"ready\":true,\"port\":%u}\n", unsigned(listener.serverPort()));
    std::fflush(stdout);

    QObject::connect(&listener, &QTcpServer::newConnection, [&] {
        while (auto* socket = listener.nextPendingConnection()) {
            if (active) { socket->abort(); socket->deleteLater(); continue; } // one client
            activeSocket = socket;
            active = std::make_unique<Connection>(socket);
            const auto drop = [&, socket] {
                if (activeSocket != socket) return;
                std::printf("client gone after %llu requests\n", active->requests);
                std::fflush(stdout);
                active.reset();
                activeSocket = nullptr;
                socket->abort();
                socket->deleteLater();
            };
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, drop] {
                if (activeSocket == socket && !active->onReadyRead()) drop();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, drop);
        }
    });

    QTimer tick;
    QObject::connect(&tick, &QTimer::timeout, [] { mydevice::device().tick(); });
    tick.start(50);
    if (const int ms = args.value(QStringLiteral("quit-after")).toInt(); ms > 0)
        QTimer::singleShot(ms, &app, &QCoreApplication::quit);
    return app.exec();
}
