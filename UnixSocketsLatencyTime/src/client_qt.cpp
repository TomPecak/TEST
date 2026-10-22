// Klient w Qt: QLocalSocket, odpowiedź odbierana w pętli zdarzeń Qt (sygnał readyRead),
// czyli tak, jak działałby kod w aplikacji Qt / QML.

#include "bench.h"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QTimer>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bench::Options options = bench::parseOptions(argc, argv, "qt");

    QLocalSocket socket;
    bench::Recorder recorder(options);
    const quint64 total = static_cast<quint64>(recorder.totalIterations());
    quint64 sequence = 0;
    int64_t start = 0;

    auto sendPing = [&]() {
        start = bench::nowNs();
        socket.write(reinterpret_cast<const char *>(&sequence), sizeof(sequence));
        socket.flush(); // od razu do jądra
    };

    // Rzadkie wywołania: przerwa przez timer, pętla zdarzeń Qt w tym czasie śpi
    auto schedulePing = [&]() {
        if (options.intervalMs() > 0)
            QTimer::singleShot(options.intervalMs(), &app, sendPing);
        else
            sendPing();
    };

    QObject::connect(&socket, &QLocalSocket::connected, &app, schedulePing);
    QObject::connect(&socket, &QLocalSocket::readyRead, &app, [&]() {
        while (socket.bytesAvailable() >= static_cast<qint64>(sizeof(quint64))) {
            const int64_t end = bench::nowNs();
            quint64 echoed = 0;
            socket.read(reinterpret_cast<char *>(&echoed), sizeof(echoed));
            if (echoed != sequence) {
                std::fprintf(stderr, "zły numer: %llu zamiast %llu\n", echoed, sequence);
                app.exit(1);
                return;
            }
            recorder.add(static_cast<long>(sequence), end - start);
            if (++sequence < total) {
                schedulePing();
            } else {
                recorder.report();
                app.quit();
                return;
            }
        }
    });
    QObject::connect(&socket, &QLocalSocket::errorOccurred, &app, [&](QLocalSocket::LocalSocketError) {
        std::fprintf(stderr, "błąd gniazda: %s\n", qPrintable(socket.errorString()));
        app.exit(1);
    });

    socket.connectToServer(QString::fromStdString(options.socketPath));
    return app.exec();
}
