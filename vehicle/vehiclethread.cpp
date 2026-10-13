// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "vehiclethread.h"
#include <vehicleservice_client.grpc.qpb.h>

#include <QtCore/QUrl>
#include <QtGrpc/QGrpcChannelOptions>
#include <QtGrpc/QGrpcHttp2Channel>

#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>

#include <unistd.h>
#include <chrono> // Dodane dla obsługi czasu w mikrosekundach

using namespace qtgrpc::examples;
using namespace google::protobuf;

VehicleThread::VehicleThread(QObject *parent) : QThread(parent)
{
}

VehicleThread::~VehicleThread() = default;

void VehicleThread::run()
{
    if (!m_client) {
        m_client = std::make_unique<qtgrpc::examples::VehicleService::Client>();
        m_client->attachChannel(
            std::make_shared<QGrpcHttp2Channel>(QUrl("unix:///tmp/qtgrpc.sock")));
    }

    //! [Speed stream]
    Empty speedRequest;
    m_streamSpeed = m_client->getSpeedStream(speedRequest);

    connect(m_streamSpeed.get(), &QGrpcServerStream::messageReceived, this, [this]() {
        if (const auto speedResponse = m_streamSpeed->read<SpeedMsg>()) {
            emit speedChanged(speedResponse->speed());
        }
    });

    connect(
        m_streamSpeed.get(), &QGrpcServerStream::finished, this,
        [this](const QGrpcStatus &status) {
            if (!status.isOk()) {
                auto error = QString("Stream error fetching speed %1 (%2)")
                .arg(status.message())
                    .arg(QVariant::fromValue(status.code()).toString());
                emit connectionError(error);
                qWarning() << error;
                return;
            }
        },
        Qt::SingleShotConnection);
    //! [Speed stream]

    //! [RPM stream]
    Empty rpmRequest;
    m_streamRpm = m_client->getRpmStream(rpmRequest);
    connect(m_streamRpm.get(), &QGrpcServerStream::messageReceived, this, [this]() {
        if (const auto rpmResponse = m_streamRpm->read<RpmMsg>()) {
            emit rpmChanged(rpmResponse->rpm());
        }
    });

    connect(
        m_streamRpm.get(), &QGrpcServerStream::finished, this,
        [this](const QGrpcStatus &status) {
            if (!status.isOk()) {
                auto error = QString("Stream error fetching RPM %1 (%2)")
                .arg(status.message())
                    .arg(QVariant::fromValue(status.code()).toString());
                emit connectionError(error);
                qWarning() << error;
                return;
            }
        },
        Qt::SingleShotConnection);
    //! [RPM stream]

    //! [Autonomy ping logic]
    QTimer *pingTimer = new QTimer();

    connect(pingTimer, &QTimer::timeout, pingTimer, [this, pingTimer]() {

        // ZMIANA: Zamiast 'Empty' używamy 'AutonomyRequest'
        AutonomyRequest autonomyRequest;

        // ZMIANA: Pobranie czasu przed wysłaniem w mikrosekundach
        auto now_client = std::chrono::system_clock::now();
        int64_t client_us = std::chrono::duration_cast<std::chrono::microseconds>(now_client.time_since_epoch()).count();
        autonomyRequest.setClientTimestamp(client_us);

        // Zostawiamy do zmierzenia pełnego RTT
        QElapsedTimer pingMeasurer;
        pingMeasurer.start();

        std::unique_ptr<QGrpcCallReply> autonomyReply = m_client->getAutonomy(autonomyRequest);
        const auto *autonomyReplyPtr = autonomyReply.get();

        connect(
            autonomyReplyPtr, &QGrpcCallReply::finished, autonomyReplyPtr,
            [this, autonomyReply = std::move(autonomyReply), pingMeasurer](const QGrpcStatus &status) {

                double rttMicroSeconds = pingMeasurer.nsecsElapsed() / 1000.0;

                if (!status.isOk()) {
                    auto error = QString("Call error fetching autonomy %1 (%2)")
                    .arg(status.message())
                        .arg(QVariant::fromValue(status.code()).toString());
                    emit connectionError(error);
                    qWarning() << error;
                    return;
                }

                if (const auto autonomyMsg = autonomyReply->read<AutonomyMsg>()) {

                    // ZMIANA: Pobranie czasu po odebraniu odpowiedzi
                    auto now_receive = std::chrono::system_clock::now();
                    int64_t current_client_time_us = std::chrono::duration_cast<std::chrono::microseconds>(now_receive.time_since_epoch()).count();

                    // Obliczenie propagacji Serwer -> Klient (odejmujemy czas wysłania przez serwer od naszego obecnego czasu)
                    int64_t server_to_client_latency = current_client_time_us - autonomyMsg->serverTimestamp();

                    qDebug() << "[Klient] RTT (całkowity czas R/W):" << rttMicroSeconds << "µs";
                    qDebug() << "[Klient] Czas propagacji Serwer -> Klient:" << server_to_client_latency << "µs";

                    // emit jest bezpieczne wielowątkowo, przekaże sygnał do GUI
                    emit autonomyChanged(autonomyMsg->autonomy());
                }
            },
            Qt::SingleShotConnection);
    });

    pingTimer->start(1000);
    //! [Autonomy ping logic]

    QThread::run();

    pingTimer->stop();
    pingTimer->deleteLater();
    m_client.reset();
}