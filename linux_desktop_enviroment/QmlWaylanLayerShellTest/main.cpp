#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegion>
#include <qqml.h>
#include <LayerShellQt/Window>

// Jeden na okno, tworzony automatycznie przez pierwszy MaskArea.
// Raz na klatkę skleja prostokąty wszystkich widocznych MaskArea w maskę okna.
class MaskTracker : public QObject {
    Q_OBJECT
public:
    static MaskTracker *forWindow(QQuickWindow *window) {
        auto *tracker = window->findChild<MaskTracker *>(QString(), Qt::FindDirectChildrenOnly);
        return tracker ? tracker : new MaskTracker(window);
    }

    void add(QQuickItem *item) { m_areas.append(item); }
    void remove(QQuickItem *item) { m_areas.removeOne(item); }

private:
    explicit MaskTracker(QQuickWindow *window) : QObject(window), m_window(window) {
        // afterAnimating: wątek GUI, raz na renderowaną klatkę
        connect(window, &QQuickWindow::afterAnimating, this, &MaskTracker::update);
    }

    void update() {
        QRegion region;
        for (QQuickItem *item : std::as_const(m_areas)) {
            if (item->isVisible()) {
                // mapRectToScene uwzględnia pozycje przodków i transformacje (scale, translate)
                region += item->mapRectToScene(QRectF(0, 0, item->width(), item->height()))
                              .toAlignedRect();
            }
        }

        // Pusty QRegion zdejmuje maskę (całe okno łapie zdarzenia),
        // więc gdy nic nie jest widoczne, dajemy region poza oknem
        if (region.isEmpty()) {
            region = QRegion(-1, -1, 1, 1);
        }

        if (region == m_last) return;
        m_last = region;
        m_window->setMask(region);
        static int updateConut = 0;
        qDebug() << "[ INFO ] Updated Mask: " << updateConut++;
    }

    QQuickWindow *m_window;
    QList<QQuickItem *> m_areas;
    QRegion m_last;
};

// Używane w QML jak MouseArea: obszar, który wypełnia, łapie zdarzenia myszy
class MaskArea : public QQuickItem {
    Q_OBJECT
public:
    using QQuickItem::QQuickItem;

    ~MaskArea() override {
        if (m_tracker) m_tracker->remove(this);
    }

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override {
        if (change == ItemSceneChange) {
            if (m_tracker) m_tracker->remove(this);
            m_tracker = data.window ? MaskTracker::forWindow(data.window) : nullptr;
            if (m_tracker) m_tracker->add(this);
        }
        QQuickItem::itemChange(change, data);
    }

private:
    QPointer<MaskTracker> m_tracker;
};

int main(int argc, char *argv[])
{
    qputenv("QT_WAYLAND_SHELL_INTEGRATION", "layer-shell");

    QGuiApplication app(argc, argv);

    // Osobny moduł, bo klasa siedzi w main.cpp (QML_ELEMENT wymaga nagłówka)
    qmlRegisterType<MaskArea>("Mask", 1, 0, "MaskArea");

    QQmlApplicationEngine engine;

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [](QObject *obj, const QUrl &objUrl) {
            if (!obj) return;

            QWindow *window = qobject_cast<QWindow *>(obj);
            if (window) {
                LayerShellQt::Window *lsWindow = LayerShellQt::Window::get(window);

                // UWAGA: Aby móc przesuwać po całym ekranie, okno musi zajmować cały ekran!
                lsWindow->setAnchors(LayerShellQt::Window::Anchors(
                    LayerShellQt::Window::AnchorTop |
                    LayerShellQt::Window::AnchorBottom |
                    LayerShellQt::Window::AnchorLeft |
                    LayerShellQt::Window::AnchorRight
                    ));

                // Ustawiamy jako nakładkę (Overlay/Top), żeby było nad innymi oknami
                lsWindow->setLayer(LayerShellQt::Window::LayerTop);
                //lsWindow->setLayer(LayerShellQt::Window::LayerBottom);

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
