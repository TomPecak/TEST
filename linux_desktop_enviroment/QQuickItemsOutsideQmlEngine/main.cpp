// #include <QGuiApplication>
// #include <QQmlApplicationEngine>

// int main(int argc, char *argv[])
// {
//     QGuiApplication app(argc, argv);

//     QQmlApplicationEngine engine;
//     QObject::connect(
//         &engine,
//         &QQmlApplicationEngine::objectCreationFailed,
//         &app,
//         []() { QCoreApplication::exit(-1); },
//         Qt::QueuedConnection);
//     engine.loadFromModule("QQuickItemsOutsideQmlEngine", "Main");

//     return QGuiApplication::exec();
// }


#include <QGuiApplication>
#include <QQuickWindow>
#include <QPropertyAnimation>
#include <private/qquickrectangle_p.h>
#include <private/qquickitem_p.h>
#include <private/qquickanchors_p.h>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickWindow window;
    window.setTitle("QQuickRectangle bez QML");
    window.resize(600, 600);
    window.setColor(Qt::white);

    auto *rect = new QQuickRectangle(window.contentItem());
    rect->setSize({300, 300});
    rect->setColor(Qt::red);
    rect->setRadius(8);

    // Odpowiednik QML: anchors.centerIn: parent
    QQuickItemPrivate::get(rect)->anchors()->setCenterIn(window.contentItem());

    auto *anim = new QPropertyAnimation(rect, "rotation", rect);
    anim->setStartValue(0.0);
    anim->setEndValue(360.0);
    anim->setDuration(2000);
    anim->setLoopCount(-1);
    anim->start();

    window.show();
    return app.exec();
}
