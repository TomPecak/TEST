// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

// #include <QtGui/QGuiApplication>
// #include <QtQml/QQmlApplicationEngine>

// int main(int argc, char *argv[])
// {
//     QGuiApplication app(argc, argv);
//     QQmlApplicationEngine engine;
//     engine.loadFromModule("qtgrpc.examples.vehicle", "Main");
//     if (engine.rootObjects().isEmpty())
//         return -1;
//     return app.exec();
// }


#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QElapsedTimer> // Dodane do mierzenia czasu
#include <QDebug>        // Dodane do wypisywania w konsoli
#include <QQuickWindow>  // Dodane do przechwycenia sygnału renderowania okna

int main(int argc, char *argv[])
{
    // 1. Rozpoczynamy pomiar czasu na samym początku programu
    QElapsedTimer timer;
    timer.start();

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    engine.loadFromModule("qtgrpc.examples.vehicle", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    // 2. Pobieramy główny obiekt QML (Twoje ApplicationWindow)
    QObject *rootObject = engine.rootObjects().first();

    // 3. Rzutujemy go na QQuickWindow, aby mieć dostęp do sygnałów renderowania
    if (QQuickWindow *window = qobject_cast<QQuickWindow *>(rootObject)) {

        // Sygnał frameSwapped jest emitowany, gdy GPU wyrenderuje klatkę na ekran
        QObject::connect(window, &QQuickWindow::frameSwapped, [window, timer]() {
            // Używamy flagi statycznej, aby wypisać to TYLKO dla pierwszej klatki
            static bool firstFrame = true;
            if (firstFrame) {
                qDebug() << "========================================";
                qDebug() << "Czas wstania GUI (do pierwszej klatki):"
                         << timer.elapsed() << "ms";
                qDebug() << "========================================";
                firstFrame = false;
            }
        });
    }

    return app.exec();
}