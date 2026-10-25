#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // Ustawienia (m.in. przypięte aplikacje): ~/.config/MaiaTest/minitaskbar.conf
    QGuiApplication::setOrganizationName(QStringLiteral("MaiaTest"));
    QGuiApplication::setApplicationName(QStringLiteral("minitaskbar"));
    // Musi pasować do nazwy pliku minitaskbar.desktop
    QGuiApplication::setDesktopFileName(QStringLiteral("minitaskbar"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("MiniTaskbar", "Main");

    return app.exec();
}
