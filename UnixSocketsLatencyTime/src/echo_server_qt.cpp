// Serwer echo w Qt: QLocalServer + QLocalSocket w pętli zdarzeń Qt.

#include "bench.h"

#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bench::Options options = bench::parseOptions(argc, argv, "echo_qt");
    const QString path = QString::fromStdString(options.socketPath);

    QLocalServer server;
    QLocalServer::removeServer(path);
    if (!server.listen(path)) {
        std::fprintf(stderr, "listen: %s\n", qPrintable(server.errorString()));
        return 1;
    }
    std::printf("echo_server_qt: nasłuchuje na %s\n", options.socketPath.c_str());
    std::fflush(stdout);

    QObject::connect(&server, &QLocalServer::newConnection, &server, [&server]() {
        while (QLocalSocket *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket]() {
                char buffer[64];
                qint64 read;
                while ((read = socket->read(buffer, sizeof(buffer))) > 0)
                    socket->write(buffer, read);
                socket->flush(); // od razu do jądra, bez czekania na pętlę zdarzeń
            });
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        }
    });

    return app.exec();
}
