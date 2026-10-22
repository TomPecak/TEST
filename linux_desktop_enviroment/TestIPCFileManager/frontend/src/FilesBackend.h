#pragma once

// Dostęp do usługi plików z QML (singleton "Files"): stan połączenia, miejsca, dyski, ostatnie
// pliki, operacje (nowy folder, zmiana nazwy, kosz, kopiowanie / przenoszenie z postępem)
// oraz pomocnicze rzeczy frontendu (ścieżki, formatowanie, ikony z motywu, otwieranie plików).
//
// Frontend sam nie czyta dysku: wszystko, co dotyczy plików, idzie przez usługę.
// Strony specjalne mają ścieżki "start" (strona główna) i "computer" (Ten komputer).

#include "ServiceConnection.h"

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

class FilesBackend : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Files)
    QML_SINGLETON

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString socketPath READ socketPath CONSTANT)
    // Opcje startowe z wiersza poleceń: { path, view, search, details }
    Q_PROPERTY(QVariantMap startOptions READ startOptions CONSTANT)
    Q_PROPERTY(QString homePath READ homePath NOTIFY infoChanged)
    Q_PROPERTY(QString trashPath READ trashPath NOTIFY infoChanged)
    Q_PROPERTY(QString serviceVersion READ serviceVersion NOTIFY infoChanged)
    Q_PROPERTY(QVariantList places READ places NOTIFY placesChanged)
    Q_PROPERTY(QVariantList drives READ drives NOTIFY drivesChanged)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    Q_PROPERTY(bool jobActive READ jobActive NOTIFY jobChanged)
    Q_PROPERTY(QString jobText READ jobText NOTIFY jobChanged)
    Q_PROPERTY(double jobProgress READ jobProgress NOTIFY jobChanged)
    // Rośnie po każdej zmianie danych, od których zależą funkcje ścieżek i ikon (katalog domowy,
    // kosz, miejsca, dyski). QML używa jej jako zależności wiązań: "Files.revision >= 0 ? ... : ..."
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QStringList quickAccess READ quickAccess NOTIFY revisionChanged)
    // Czas odpowiedzi ostatniego prostego wywołania (info) w ms, do podglądu opóźnienia IPC
    Q_PROPERTY(double lastCallMs READ lastCallMs NOTIFY infoChanged)
    // Ikony z motywu rysowane pod ciemne tło (ustawiane z QML według Theme.dark)
    Q_PROPERTY(bool darkIcons READ darkIcons WRITE setDarkIcons NOTIFY darkIconsChanged)

public:
    explicit FilesBackend(const QString &socketPath, QObject *parent = nullptr);
    ~FilesBackend() override;

    static FilesBackend *instance();
    static FilesBackend *create(QQmlEngine *, QJSEngine *);

    ServiceConnection *connection() const { return m_connection; }
    QVariantMap startOptions() const { return m_startOptions; }
    void setStartOptions(const QVariantMap &options) { m_startOptions = options; }

    bool connected() const { return m_connection->isConnected(); }
    QString socketPath() const { return m_connection->socketPath(); }
    QString homePath() const { return m_homePath; }
    QString trashPath() const { return m_trashPath; }
    QString serviceVersion() const { return m_serviceVersion; }
    QVariantList places() const { return m_places; }
    QVariantList drives() const { return m_drives; }
    QVariantList recent() const { return m_recent; }
    bool jobActive() const { return m_currentJob != 0; }
    QString jobText() const { return m_jobText; }
    double jobProgress() const { return m_jobProgress; }
    double lastCallMs() const { return m_lastCallMs; }
    int revision() const { return m_revision; }
    bool darkIcons() const { return m_darkIcons; }
    void setDarkIcons(bool dark);

    Q_INVOKABLE void refreshDrives();
    Q_INVOKABLE void refreshRecent();
    Q_INVOKABLE void ping();

    // callback(name, error)
    Q_INVOKABLE void createFolder(const QString &parent, const QString &baseName, const QJSValue &callback);
    // callback(ok, message)
    Q_INVOKABLE void rename(const QString &parent, const QString &oldName, const QString &newName, const QJSValue &callback);
    // callback(ok, message) po zakończeniu zadania; postęp w jobText / jobProgress
    Q_INVOKABLE void trash(const QStringList &paths, const QJSValue &callback);
    Q_INVOKABLE void transfer(const QStringList &sources, const QString &destination, bool move, const QJSValue &callback);
    Q_INVOKABLE void cancelJob();

    // --- Ścieżki ---
    Q_INVOKABLE bool isFolderPath(const QString &path) const;
    Q_INVOKABLE bool isInTrash(const QString &path) const;
    Q_INVOKABLE QString displayName(const QString &path) const;
    Q_INVOKABLE QString parentPath(const QString &path) const;
    Q_INVOKABLE QString joinPath(const QString &folder, const QString &name) const;
    // Okruszki paska adresu: [{ name, path }]
    Q_INVOKABLE QVariantList segments(const QString &path) const;
    // Lokalizacja do edycji w pasku adresu i z powrotem ("" gdy tekst nie jest ścieżką)
    Q_INVOKABLE QString toLocation(const QString &path) const;
    Q_INVOKABLE QString fromLocation(const QString &text) const;
    // Ścieżka do pokazania (katalog domowy jako ~)
    Q_INVOKABLE QString shortPath(const QString &path) const;
    QStringList quickAccess() const;

    // --- Formatowanie ---
    Q_INVOKABLE QString formatSize(double bytes) const;
    Q_INVOKABLE QString formatDate(double ms) const;
    Q_INVOKABLE QString formatRelative(double ms) const;

    // --- Ikony i aplikacje ---
    // Nazwa ikony z motywu: konkretna, a gdy motyw jej nie ma, ogólna dla typu
    Q_INVOKABLE QString themeIcon(const QString &iconName, const QString &mimeType, bool isDir) const;
    // Ikona folderu albo strony (foldery XDG, kosz, dyski mają własne)
    Q_INVOKABLE QString folderIcon(const QString &path) const;
    // Otwiera plik domyślną aplikacją, folder w terminalu
    Q_INVOKABLE void openFile(const QString &path) const;
    Q_INVOKABLE void openTerminal(const QString &path) const;

signals:
    void connectedChanged();
    void infoChanged();
    void placesChanged();
    void drivesChanged();
    void recentChanged();
    void jobChanged();
    void revisionChanged();
    void darkIconsChanged();

private:
    void loadStartupData();
    void bumpRevision();
    quint64 storeCallback(const QJSValue &callback);
    void invokeCallback(quint64 id, const QJSValueList &arguments);
    void startJob(quint64 callbackId, const QString &text);
    void handleJobEvent(quint64 callbackId, const JobEvent &event);

    ServiceConnection *m_connection;
    QVariantMap m_startOptions;
    QString m_homePath;
    QString m_trashPath;
    QString m_serviceVersion;
    double m_lastCallMs = 0;
    int m_revision = 0;
    bool m_darkIcons = false;
    QVariantList m_places;
    QVariantList m_drives;
    QVariantList m_recent;
    QHash<QString, QString> m_placeIcons;   // ścieżka -> ikona (foldery XDG, dyski)

    // Wywołania zwrotne QML (QJSValue zostaje w wątku GUI; wątek KJ zna tylko identyfikator)
    QHash<quint64, QJSValue> m_callbacks;
    quint64 m_nextCallback = 1;

    quint64 m_currentJob = 0;     // identyfikator zadania w ServiceConnection
    QString m_jobText;
    double m_jobProgress = 0;

    mutable QHash<QString, QString> m_iconCache;
};
