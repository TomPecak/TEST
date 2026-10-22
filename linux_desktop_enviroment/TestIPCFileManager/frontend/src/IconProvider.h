#pragma once

// Kolorowe ikony z motywu (image://icon/<nazwa>).
//
// Ikony Breeze z Plasmy 6 mają w SVG kolory "currentColor" i klasy ColorScheme-*; koloruje je
// silnik ikon platformy (KIconEngine), do którego prowadzi QIcon::fromTheme. IconImage z Qt Quick
// wczytuje SVG z pominięciem tego silnika, więc foldery wychodzą czarne - stąd ten dostawca.

#include <QIcon>
#include <QQuickImageProvider>

class IconProvider : public QQuickImageProvider
{
public:
    IconProvider()
        : QQuickImageProvider(QQuickImageProvider::Pixmap)
    {
    }

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        // requestedSize jest już w pikselach urządzenia (sourceSize * devicePixelRatio)
        const QSize pixels = requestedSize.isValid() ? requestedSize : QSize(64, 64);
        // "nazwa?dark" / "nazwa?light": wariant w adresie, żeby po zmianie motywu ikony
        // wczytały się na nowo (z nową paletą) zamiast z pamięci podręcznej
        const QString name = id.section(QLatin1Char('?'), 0, 0);
        QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("unknown"));
        QPixmap pixmap = icon.pixmap(pixels, 1.0);
        if (pixmap.isNull()) {
            // Brak motywu ikon (np. bez sesji graficznej): pusta ikona zamiast błędu w logu
            pixmap = QPixmap(pixels);
            pixmap.fill(Qt::transparent);
        }
        if (size)
            *size = pixmap.size();
        return pixmap;
    }
};
