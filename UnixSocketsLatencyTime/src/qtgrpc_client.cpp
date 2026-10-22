// Klient QtGrpc: kanał QGrpcHttp2Channel na gnieździe Unix, odpowiedź odbierana w pętli
// zdarzeń Qt (sygnał finished), czyli tak, jak w aplikacji Qt / QML (np. przykład vehicle).

#include "bench.h"
#include "latency.qpb.h"
#include "latency_client.grpc.qpb.h"

#include <QCoreApplication>
#include <QTimer>
#include <QUrl>
#include <QtGrpc/QGrpcHttp2Channel>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bench::Options options = bench::parseOptions(argc, argv, "qt-grpc");

    latency::PingService::Client client;
    client.attachChannel(std::make_shared<QGrpcHttp2Channel>(
        QUrl(QString::fromStdString("unix://" + options.socketPath))));

    bench::Recorder recorder(options);
    const quint64 total = static_cast<quint64>(recorder.totalIterations());
    quint64 sequence = 0;

    std::function<void()> sendPing;
    std::function<void()> schedulePing = [&]() {
        if (options.intervalMs() > 0)
            QTimer::singleShot(options.intervalMs(), &app, sendPing);
        else
            sendPing(); // na gorąco: od razu, jak w client_qt
    };

    sendPing = [&]() {
        latency::PingMsg request;
        request.setSeq(sequence);

        const int64_t start = bench::nowNs();
        std::unique_ptr<QGrpcCallReply> reply = client.ping(request);
        QGrpcCallReply *replyPtr = reply.get();

        QObject::connect(
            replyPtr, &QGrpcCallReply::finished, &app,
            [&, start, reply = std::move(reply)](const QGrpcStatus &status) mutable {
                const int64_t end = bench::nowNs();
                const auto response = reply->read<latency::PingMsg>();
                if (!status.isOk() || !response || quint64(response->seq()) != sequence) {
                    std::fprintf(stderr, "błąd wywołania: %s\n", qPrintable(status.message()));
                    app.exit(1);
                    return;
                }
                recorder.add(static_cast<long>(sequence), end - start);
                // Odpowiedź usuwamy po powrocie z sygnału, nie w jego trakcie
                reply.release()->deleteLater();

                if (++sequence < total) {
                    schedulePing();
                } else {
                    recorder.report();
                    app.quit();
                }
            },
            Qt::SingleShotConnection);
    };

    schedulePing();
    return app.exec();
}
