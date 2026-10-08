#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QFontDatabase>
#include <QQmlContext>

#include "FontModel.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QDir fontDir("fonts/");
    foreach (QString fontFile, fontDir.entryList(QStringList() << "*.ttf" << "*.otf", QDir::Files)) {
        qDebug() << "Font file:" << fontFile;
        QFontDatabase::addApplicationFont(fontDir.absoluteFilePath(fontFile));
    }


    FontModel fontModel;
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("fontModel", &fontModel);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("KIKOFontViewer", "Main");

    return app.exec();
}

