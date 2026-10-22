#include <QAnimationDriver>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QElapsedTimer>

// Taktuje wszystkie animacje w procesie klatkami jednego okna ("master").
// Zastępuje sterownik Qt, który przy więcej niż jednym widocznym oknie
// przechodzi z vsync na zwykły timer.
// class WindowAnimationDriver : public QAnimationDriver {
//     Q_OBJECT
// public:
//     using QAnimationDriver::QAnimationDriver;

//     void addWindow(QQuickWindow *window) {
//         m_windows.append(window);
//         // afterAnimating: wątek GUI, raz na klatkę okna, przed synchronizacją sceny
//         connect(window, &QQuickWindow::afterAnimating, this, [this, window] {
//             if (!isRunning()) return;
//             advance();
//             window->update(); // podtrzymaj klatki, dopóki coś się animuje
//         });
//         if (isRunning()) window->update();
//     }

// protected:
//     void start() override {
//         QAnimationDriver::start();
//         for (const auto &window : std::as_const(m_windows)) {
//             if (window) window->update();
//         }
//     }

// private:
//     QList<QPointer<QQuickWindow>> m_windows;
// };

// class WindowAnimationDriver : public QAnimationDriver {
//     Q_OBJECT
// public:
//     using QAnimationDriver::QAnimationDriver;

//     void addWindow(QQuickWindow *window) {
//         m_windows.append(window);
//         connect(window, &QQuickWindow::afterAnimating, this, [this, window] {
//             if (!isRunning()) return;
//             latchFrameTime(window);
//             advance();
//             window->update();
//         });
//         if (isRunning()) window->update();
//     }

//     // Qt pyta o czas tutaj; advance() nie przyjmuje go jako argumentu
//     qint64 elapsed() const override { return qRound64(m_frameTime); }

// protected:
//     void start() override {
//         m_clock.start();
//         m_frameTime = 0;
//         QAnimationDriver::start();
//         for (const auto &window : std::as_const(m_windows)) {
//             if (window) window->update();
//         }
//     }

// private:
//     void latchFrameTime(QQuickWindow *window) {
//         const double interval = 1000.0 / window->screen()->refreshRate();
//         const double behind = m_clock.nsecsElapsed() / 1e6 - m_frameTime;
//         // mniej niż pół okresu od ostatniego zatrzasku: to samo odświeżenie, ten sam czas
//         if (behind < interval / 2) return;
//         // nowe odświeżenie: przesuń o całkowitą liczbę kwantów (więcej niż 1 przy zgubionych klatkach)
//         m_frameTime += qMax(1.0, std::round(behind / interval)) * interval;
//     }

//     QList<QPointer<QQuickWindow>> m_windows;
//     QElapsedTimer m_clock;
//     double m_frameTime = 0; // ms
// };

class WindowAnimationDriver : public QAnimationDriver {
    Q_OBJECT
public:
    using QAnimationDriver::QAnimationDriver;

    void setMaster(QQuickWindow *window) {
        m_master = window;
        m_interval = 1000.0 / window->screen()->refreshRate();
        connect(window, &QQuickWindow::afterAnimating, this, [this] {
            if (!isRunning()) return;
            tick();
            advance();
            m_master->update(); // podtrzymaj klatki, dopóki coś się animuje
        });
        if (isRunning()) m_master->update();
    }

    // Qt pyta o czas tutaj: n_klatek * okres
    qint64 elapsed() const override { return qRound64(m_frames * m_interval); }

protected:
    void start() override {
        m_clock.start();
        m_frames = 0;
        QAnimationDriver::start();
        if (m_master) m_master->update();
    }

private:
    void tick() {
        ++m_frames;
        // ile kwantów minęło naprawdę; korekta tylko przy zgubionych klatkach
        // albo gdy ekran odświeża inaczej, niż deklaruje
        const qint64 wall = qRound64(m_clock.nsecsElapsed() / 1e6 / m_interval);
        if (qAbs(wall - m_frames) >= 2) m_frames = wall;
    }

    QPointer<QQuickWindow> m_master;
    QElapsedTimer m_clock;
    double m_interval = 1000.0 / 60; // ms
    qint64 m_frames = 0;
};

int main(int argc, char *argv[])
{
    //qputenv("QSG_RENDER_LOOP","basic" );
    QGuiApplication app(argc, argv);

    // --no-driver: zostaw domyślny sterownik Qt, do porównania
    //const bool useDriver = !app.arguments().contains("--no-driver");
    const bool useDriver = true;

    // Musi być zainstalowany PRZED utworzeniem pierwszego okna Qt Quick.
    // Qt wypisze potem "QUnifiedTimer: animation driver already installed..." -
    // to odrzucony sterownik Qt, tak ma być.
    WindowAnimationDriver driver;
    if (useDriver) driver.install();

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("AnimationDriverTest", "Main");

    if (useDriver) {
        const auto windows = QGuiApplication::allWindows();
        for (QWindow *window : windows) {
            if(window->objectName() == "master"){
                driver.setMaster(qobject_cast<QQuickWindow *>(window));
            }
            // if (auto *quickWindow = qobject_cast<QQuickWindow *>(window)) {
            //     driver.addWindow(quickWindow);
            // }
        }
    }

    qInfo("Sterownik animacji: %s", useDriver ? "własny (klatki okna master)" : "domyślny Qt");

    return app.exec();
}

#include "main.moc" // Konieczne ze względu na Q_OBJECT w jednym pliku
