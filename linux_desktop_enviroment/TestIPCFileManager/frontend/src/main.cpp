// Frontend testowego menedżera plików: Qt Quick + modele C++, dane z usługi Rust przez Cap'n Proto.

#include "FilesBackend.h"
#include "IconProvider.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QDebug>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("TestIPC"));
    app.setApplicationName(QStringLiteral("TestFiles"));

    // Kolorowe ikony plików pochodzą z motywu ikon; bez motywu platformy bierzemy Breeze
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    QString socket = qEnvironmentVariable("TEST_IPC_FILES_SOCKET");
    if (socket.isEmpty()) {
        const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR", QStringLiteral("/tmp"));
        socket = runtime + QStringLiteral("/test-ipc-files.sock");
    }
    // Singleton "Files" musi istnieć, zanim QML utworzy pierwszy FolderModel
    FilesBackend backend(socket);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Test file manager (Qt Quick frontend, Rust service over Cap'n Proto)"));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Folder to open"));
    parser.addOption({QStringLiteral("view"), QStringLiteral("details, large or medium"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("search"), QStringLiteral("Search in the folder"), QStringLiteral("text")});
    parser.addOption({QStringLiteral("details"), QStringLiteral("Show the details pane")});
    parser.addOption({QStringLiteral("selftest"), QStringLiteral("Run the operations (new folder, rename, copy, Recycle Bin) through the UI functions")});
    parser.addOption({QStringLiteral("bench"), QStringLiteral("Visit the given folders one after another (twice) and log the load times")});
    parser.process(app);
    QVariantMap options;
    QStringList paths;
    for (const QString &path : parser.positionalArguments())
        paths.append(path == QLatin1String("start") || path == QLatin1String("computer") ? path : QDir(path).absolutePath());
    const bool bench = parser.isSet(QStringLiteral("bench"));
    if (!paths.isEmpty() && !bench)
        options.insert(QStringLiteral("path"), paths.first());
    options.insert(QStringLiteral("bench"), bench);
    options.insert(QStringLiteral("selftest"), parser.isSet(QStringLiteral("selftest")));
    options.insert(QStringLiteral("benchPaths"), paths + paths);
    options.insert(QStringLiteral("view"), parser.value(QStringLiteral("view")));
    options.insert(QStringLiteral("search"), parser.value(QStringLiteral("search")));
    options.insert(QStringLiteral("details"), parser.isSet(QStringLiteral("details")));
    backend.setStartOptions(options);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("TestFiles", "Main");
    return app.exec();
}
