#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>
#include <QRegion>
#include <LayerShellQt/Window>

// Klasa pomocnicza udostępniona do QML
class MaskHelper : public QObject {
    Q_OBJECT
    QWindow* m_window = nullptr;

public:
    explicit MaskHelper(QObject *parent = nullptr) : QObject(parent) {}

    void setWindow(QWindow* window) {
        m_window = window;
    }

    // Wywoływane, gdy puszczamy dock - maska ogranicza się do jego rozmiarów
    Q_INVOKABLE void setRectMask(int x, int y, int w, int h) {
        if (m_window) {
            m_window->setMask(QRegion(x, y, w, h));
        }
    }

    // Wywoływane, gdy łapiemy dock - zdejmujemy maskę, całe okno łapie zdarzenia
    Q_INVOKABLE void setFullMask() {
        if (m_window) {
            // Pusty QRegion zdejmuje maskę, przywracając pełny obszar okna
            m_window->setMask(QRegion());
        }
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    // Tworzymy helpera i udostępniamy w QML jako "SystemMask"
    MaskHelper maskHelper;
    engine.rootContext()->setContextProperty("SystemMask", &maskHelper);

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [&maskHelper](QObject *obj, const QUrl &objUrl) {
            if (!obj) return;

            QWindow *window = qobject_cast<QWindow *>(obj);
            if (window) {
                maskHelper.setWindow(window);

                LayerShellQt::Window *lsWindow = LayerShellQt::Window::get(window);

                // UWAGA: Aby móc przesuwać po całym ekranie, okno musi zajmować cały ekran!
                lsWindow->setAnchors(LayerShellQt::Window::Anchors(
                    LayerShellQt::Window::AnchorTop |
                    LayerShellQt::Window::AnchorBottom |
                    LayerShellQt::Window::AnchorLeft |
                    LayerShellQt::Window::AnchorRight
                    ));

                // Ustawiamy jako nakładkę (Overlay/Top), żeby było nad innymi oknami
                //lsWindow->setLayer(LayerShellQt::Window::LayerTop);
                lsWindow->setLayer(LayerShellQt::Window::LayerBottom);

                // UWAGA 2: Okno pełnoekranowe NIE MOŻE mieć strefy ekskluzywnej.
                // Inaczej KWin/Wayland zwariuje. Musi być 0.
                lsWindow->setExclusiveZone(0);
            }
        },
        Qt::QueuedConnection);

    engine.loadFromModule("QmlWaylanLayerShellTest", "Main");

    return app.exec();
}

#include "main.moc" // Konieczne ze względu na Q_OBJECT w jednym pliku