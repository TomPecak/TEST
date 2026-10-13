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
//     engine.loadFromModule("TestCpuUsageKDE", "Main");

//     return app.exec();
// }


#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#include <sensors/Sensor.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    qDebug() << "Rozpoczynam monitorowanie (aplikacja zamknie się automatycznie za 30 sekund)...";
    qDebug() << "---------------------------------------------------";

    // 1. Użycie CPU (całkowite)
    auto cpuUsage = new KSysGuard::Sensor("cpu/all/usage");
    QObject::connect(cpuUsage, &KSysGuard::Sensor::valueChanged, [cpuUsage]() {
        qDebug() << "[CPU] Użycie:" << cpuUsage->formattedValue();
    });

    // 2. Temperatura CPU (odczytujemy temperaturę rdzenia 0 jako główną)
    auto cpuTemp = new KSysGuard::Sensor("cpu/cpu0/temperature");
    QObject::connect(cpuTemp, &KSysGuard::Sensor::valueChanged, [cpuTemp]() {
        qDebug() << "[CPU] Temperatura:" << cpuTemp->formattedValue();
    });

    // 3. Użycie GPU (karta gpu1 z Twoich logów)
    auto gpuUsage = new KSysGuard::Sensor("gpu/gpu1/usage");
    QObject::connect(gpuUsage, &KSysGuard::Sensor::valueChanged, [gpuUsage]() {
        qDebug() << "[GPU] Użycie:" << gpuUsage->formattedValue();
    });

    // 4. Temperatura GPU
    auto gpuTemp = new KSysGuard::Sensor("gpu/gpu1/temperature");
    QObject::connect(gpuTemp, &KSysGuard::Sensor::valueChanged, [gpuTemp]() {
        qDebug() << "[GPU] Temperatura:" << gpuTemp->formattedValue();
    });

    // Uruchom aplikację przez 30 sekund, abyś mógł na spokojnie obserwować wartości
    QTimer::singleShot(30000, &app, &QCoreApplication::quit);

    return app.exec();
}
