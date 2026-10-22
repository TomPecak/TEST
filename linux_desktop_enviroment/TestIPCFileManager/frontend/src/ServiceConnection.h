#pragma once

// Połączenie z usługą plików (backend w Ruście) przez Cap'n Proto RPC.
//
// Cap'n Proto w C++ działa na własnej pętli zdarzeń (KJ), więc całe RPC żyje w osobnym wątku.
// Wątek GUI nigdy nie czeka na usługę:
//   - zlecenia z GUI trafiają do kolejki i budzą wątek KJ przez eventfd (post()),
//   - odpowiedzi i zdarzenia wracają do wątku GUI sygnałami kolejkowymi (invokeMethod).
// Po rozłączeniu (np. restart usługi) wątek łączy się ponownie co sekundę; modele subskrybują
// foldery od nowa po sygnale connectedChanged(true).

#include "files.capnp.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace kj {
class TaskSet;
}

// Wpis folderu po stronie Qt (kopia danych z wiadomości Cap'n Proto)
struct FileEntry {
    QString name;
    bool isDir = false;
    bool isSymlink = false;
    bool isHidden = false;
    quint64 size = 0;
    qint64 modified = 0;
    QString mimeType;
    QString iconName;
    QString typeName;
    QString location;
};

// Zdarzenie od słuchacza folderu (migawka, zmiany na żywo, błąd)
struct DirectoryEvent {
    enum Type { Batch, Done, Added, Removed, Changed, Failed };
    Type type = Batch;
    QVector<FileEntry> entries;
    QStringList names;
    quint32 count = 0;
    QString message;
    // Czas od wysłania zlecenia do nadejścia zdarzenia w wątku RPC (ms): sama usługa + IPC,
    // bez czekania na wątek GUI. Dla partii migawki i jej końca, w pozostałych -1.
    double serviceMs = -1;
};

// Odbiorca zdarzeń folderu w wątku GUI (FolderModel)
class DirectoryReceiver
{
public:
    virtual ~DirectoryReceiver() = default;
    virtual void directoryEvent(const DirectoryEvent &event) = 0;
};

// Zdarzenie zadania (kosz, kopiowanie, przenoszenie)
struct JobEvent {
    bool finished = false;
    bool ok = false;
    QString message;
    quint64 doneBytes = 0;
    quint64 totalBytes = 0;
    quint32 doneItems = 0;
    quint32 totalItems = 0;
    QString current;
};

// Stan dostępny tylko w wątku KJ
struct KjContext {
    FileService::Client service;
    std::unordered_map<quint64, capnp::Capability::Client> handles; // aktywne subskrypcje i zadania
    kj::TaskSet *tasks = nullptr;
};

class ServiceConnection : public QObject
{
    Q_OBJECT

public:
    using Task = std::function<void(KjContext &)>;

    explicit ServiceConnection(const QString &socketPath, QObject *parent = nullptr);
    ~ServiceConnection() override;

    bool isConnected() const { return m_connected; }
    QString socketPath() const { return m_socketPath; }

    // Wykonuje zadanie w wątku KJ (tylko gdy połączenie działa; w przeciwnym razie odrzuca)
    void post(Task task);

    // Subskrypcje: zwracają identyfikator; zdarzenia trafiają do receiver w wątku GUI
    quint64 watchDirectory(const QString &path, QObject *receiver);
    quint64 search(const QString &path, const QString &query, QObject *receiver);
    void unsubscribe(quint64 id);

    // Zadania z postępem; callback w wątku GUI dla każdego zdarzenia
    quint64 trash(const QStringList &paths, std::function<void(const JobEvent &)> callback);
    quint64 transfer(const QStringList &sources, const QString &destination, bool move,
                     std::function<void(const JobEvent &)> callback);
    void cancelJob(quint64 id);

    // Wywoływane z wątku KJ (przez słuchaczy), przekazują dane do wątku GUI
    void deliverDirectoryEvent(quint64 id, DirectoryEvent event);
    void deliverJobEvent(quint64 id, JobEvent event);

    // Wywołanie f w wątku GUI (z dowolnego wątku)
    template<typename F>
    void toGui(F f)
    {
        QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
    }

signals:
    void connectedChanged(bool connected);

private:
    void run();
    void wake();
    void setConnected(bool connected);

    QString m_socketPath;
    bool m_connected = false; // tylko wątek GUI

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    int m_eventFd = -1;
    std::mutex m_mutex;
    std::deque<Task> m_queue;

    std::atomic<quint64> m_nextId{1};
    QHash<quint64, QPointer<QObject>> m_directoryReceivers;                     // wątek GUI
    QHash<quint64, std::function<void(const JobEvent &)>> m_jobCallbacks;       // wątek GUI
};

// Konwersje między Qt a Cap'n Proto
FileEntry toFileEntry(Entry::Reader reader);
QString toQString(capnp::Text::Reader text);
