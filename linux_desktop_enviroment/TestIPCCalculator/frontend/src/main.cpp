// Frontend testowego kalkulatora: Qt Quick (motyw Lumen), liczenie w usłudze Rust przez Cap'n Proto.

#include "CalculatorBackend.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TestIPC"));
    app.setApplicationName(QStringLiteral("TestCalculator"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Test calculator (Qt Quick frontend, Rust service over Cap'n Proto)"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("selftest"), QStringLiteral("Press keys through the service and check the results")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Save the window to a PNG file and quit"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("screenshot-delay"), QStringLiteral("Delay before the screenshot (ms)"), QStringLiteral("ms"), QStringLiteral("1500")});
    parser.addOption({QStringLiteral("size"), QStringLiteral("Window size, e.g. 340x560"), QStringLiteral("WxH")});
    parser.addOption({QStringLiteral("panel"), QStringLiteral("Open the side panel: history or memory"), QStringLiteral("tab")});
    parser.addOption({QStringLiteral("anim-bench"), QStringLiteral("Toggle a panel of the iPad face and log how busy the GUI thread is: extra or sidebar"), QStringLiteral("panel")});
    parser.addOption({QStringLiteral("resize-bench"), QStringLiteral("Resize the window step by step (like dragging an edge) and log how busy the GUI thread is")});
    parser.addOption({QStringLiteral("keys"), QStringLiteral("Keys to press after the start, e.g. 1,2,add"), QStringLiteral("list")});
    parser.addOption({QStringLiteral("face"), QStringLiteral("Look of the calculator: lumen or ipad (remembered)"), QStringLiteral("name")});
    parser.process(app);

    QString socket = qEnvironmentVariable("TEST_IPC_CALCULATOR_SOCKET");
    if (socket.isEmpty())
        socket = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/tmp")) + QStringLiteral("/test-ipc-calculator.sock");

    // Singleton "Calc" musi istnieć, zanim QML go użyje
    CalculatorBackend backend(socket);
    QVariantMap options{
        {QStringLiteral("selftest"), parser.isSet(QStringLiteral("selftest"))},
        {QStringLiteral("panel"), parser.value(QStringLiteral("panel"))},
        {QStringLiteral("face"), parser.value(QStringLiteral("face"))},
        {QStringLiteral("keys"), parser.value(QStringLiteral("keys"))},
        {QStringLiteral("resizeBench"), parser.isSet(QStringLiteral("resize-bench"))},
        {QStringLiteral("animBench"), parser.value(QStringLiteral("anim-bench"))},
    };
    const QStringList size = parser.value(QStringLiteral("size")).split(QLatin1Char('x'));
    if (size.size() == 2) {
        options.insert(QStringLiteral("width"), size.at(0).toInt());
        options.insert(QStringLiteral("height"), size.at(1).toInt());
    }
    backend.setStartOptions(options);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TestCalculator", "Main");

    // Zrzut okna (do testów bez kompozytora: QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software)
    const QString screenshot = parser.value(QStringLiteral("screenshot"));
    if (!screenshot.isEmpty() && !engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        QTimer::singleShot(parser.value(QStringLiteral("screenshot-delay")).toInt(), &app, [window, screenshot] {
            const bool saved = window && window->grabWindow().save(screenshot);
            qInfo().noquote() << (saved ? "[screenshot] zapisano" : "[screenshot] BŁĄD") << screenshot;
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
