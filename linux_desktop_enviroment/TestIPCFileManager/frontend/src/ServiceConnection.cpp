#include "ServiceConnection.h"

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/debug.h>

#include <QDebug>

#include <chrono>

#include <sys/eventfd.h>
#include <unistd.h>

namespace {

class LogErrors final : public kj::TaskSet::ErrorHandler
{
public:
    void taskFailed(kj::Exception &&exception) override
    {
        qWarning() << "[rpc]" << exception.getDescription().cStr();
    }
};

// Słuchacz folderu po stronie klienta: wywoływany przez usługę w wątku KJ
class DirectoryListenerImpl final : public DirectoryListener::Server
{
public:
    DirectoryListenerImpl(ServiceConnection *connection, quint64 id)
        : m_connection(connection)
        , m_id(id)
        , m_start(std::chrono::steady_clock::now())
    {
    }

protected:
    kj::Promise<void> batch(BatchContext context) override
    {
        return deliver(DirectoryEvent::Batch, context.getParams().getEntries());
    }

    kj::Promise<void> added(AddedContext context) override
    {
        return deliver(DirectoryEvent::Added, context.getParams().getEntries());
    }

    kj::Promise<void> changed(ChangedContext context) override
    {
        return deliver(DirectoryEvent::Changed, context.getParams().getEntries());
    }

    kj::Promise<void> removed(RemovedContext context) override
    {
        DirectoryEvent event;
        event.type = DirectoryEvent::Removed;
        for (auto name : context.getParams().getNames())
            event.names.append(toQString(name));
        m_connection->deliverDirectoryEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

    kj::Promise<void> snapshotDone(SnapshotDoneContext context) override
    {
        DirectoryEvent event;
        event.type = DirectoryEvent::Done;
        event.count = context.getParams().getCount();
        event.serviceMs = elapsedMs();
        m_connection->deliverDirectoryEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

    kj::Promise<void> failed(FailedContext context) override
    {
        DirectoryEvent event;
        event.type = DirectoryEvent::Failed;
        event.message = toQString(context.getParams().getMessage());
        m_connection->deliverDirectoryEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

private:
    kj::Promise<void> deliver(DirectoryEvent::Type type, capnp::List<Entry>::Reader entries)
    {
        DirectoryEvent event;
        event.type = type;
        if (type == DirectoryEvent::Batch)
            event.serviceMs = elapsedMs();
        event.entries.reserve(entries.size());
        for (auto entry : entries)
            event.entries.append(toFileEntry(entry));
        m_connection->deliverDirectoryEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

    double elapsedMs() const
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
    }

    ServiceConnection *m_connection;
    quint64 m_id;
    std::chrono::steady_clock::time_point m_start;
};

class JobListenerImpl final : public JobListener::Server
{
public:
    JobListenerImpl(ServiceConnection *connection, quint64 id)
        : m_connection(connection)
        , m_id(id)
    {
    }

protected:
    kj::Promise<void> progress(ProgressContext context) override
    {
        auto params = context.getParams();
        JobEvent event;
        event.doneBytes = params.getDoneBytes();
        event.totalBytes = params.getTotalBytes();
        event.doneItems = params.getDoneItems();
        event.totalItems = params.getTotalItems();
        event.current = toQString(params.getCurrent());
        m_connection->deliverJobEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

    kj::Promise<void> finished(FinishedContext context) override
    {
        JobEvent event;
        event.finished = true;
        event.ok = context.getParams().getOk();
        event.message = toQString(context.getParams().getMessage());
        m_connection->deliverJobEvent(m_id, std::move(event));
        return kj::READY_NOW;
    }

private:
    ServiceConnection *m_connection;
    quint64 m_id;
};

} // namespace

QString toQString(capnp::Text::Reader text)
{
    return QString::fromUtf8(text.begin(), static_cast<qsizetype>(text.size()));
}

FileEntry toFileEntry(Entry::Reader reader)
{
    FileEntry entry;
    entry.name = toQString(reader.getName());
    entry.isDir = reader.getIsDir();
    entry.isSymlink = reader.getIsSymlink();
    entry.isHidden = reader.getIsHidden();
    entry.size = reader.getSize();
    entry.modified = reader.getModified();
    entry.mimeType = toQString(reader.getMimeType());
    entry.iconName = toQString(reader.getIconName());
    entry.typeName = toQString(reader.getTypeName());
    entry.location = toQString(reader.getLocation());
    return entry;
}

ServiceConnection::ServiceConnection(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_socketPath(socketPath)
{
    m_eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_thread = std::thread([this] { run(); });
}

ServiceConnection::~ServiceConnection()
{
    m_quit = true;
    wake();
    m_thread.join();
    close(m_eventFd);
}

void ServiceConnection::wake()
{
    const uint64_t one = 1;
    [[maybe_unused]] auto written = ::write(m_eventFd, &one, sizeof(one));
}

void ServiceConnection::post(Task task)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(task));
    }
    wake();
}

void ServiceConnection::setConnected(bool connected)
{
    toGui([this, connected] {
        if (m_connected == connected)
            return;
        m_connected = connected;
        emit connectedChanged(connected);
    });
}

void ServiceConnection::run()
{
    kj::AsyncIoContext io = kj::setupAsyncIo();
    kj::UnixEventPort::FdObserver wakeObserver(io.unixEventPort, m_eventFd,
                                               kj::UnixEventPort::FdObserver::OBSERVE_READ);
    auto drainWake = [this] {
        uint64_t value;
        [[maybe_unused]] auto read = ::read(m_eventFd, &value, sizeof(value));
    };
    LogErrors errors;
    bool reportedFailure = false;

    while (!m_quit) {
        bool wasConnected = false;
        try {
            auto address = io.provider->getNetwork()
                               .parseAddress(kj::str("unix:", m_socketPath.toUtf8().constData()))
                               .wait(io.waitScope);
            auto stream = address->connect().wait(io.waitScope);
            capnp::TwoPartyClient client(*stream);

            KjContext context{client.bootstrap().castAs<FileService>()};
            kj::TaskSet tasks(errors);
            context.tasks = &tasks;

            bool disconnected = false;
            auto disconnect = client.onDisconnect()
                                  .then([&] { disconnected = true; },
                                        [&](kj::Exception &&) { disconnected = true; })
                                  .fork();

            wasConnected = true;
            reportedFailure = false;
            setConnected(true);
            qInfo() << "[rpc] połączono z" << m_socketPath;

            while (!m_quit && !disconnected) {
                // Czekamy na zlecenie z GUI albo na rozłączenie; w tym czasie pętla KJ
                // obsługuje RPC (odpowiedzi, wywołania słuchaczy).
                wakeObserver.whenBecomesReadable().exclusiveJoin(disconnect.addBranch()).wait(io.waitScope);
                drainWake();

                std::deque<Task> queue;
                {
                    std::lock_guard lock(m_mutex);
                    queue.swap(m_queue);
                }
                for (auto &task : queue) {
                    try {
                        task(context);
                    } catch (const kj::Exception &exception) {
                        qWarning() << "[rpc]" << exception.getDescription().cStr();
                    }
                }
            }
        } catch (const kj::Exception &exception) {
            if (!reportedFailure)
                qWarning() << "[rpc] brak połączenia z usługą:" << exception.getDescription().cStr();
            reportedFailure = true;
        }

        if (wasConnected) {
            setConnected(false);
            qInfo() << "[rpc] rozłączono, ponawiam połączenie";
        }
        {
            // Zlecenia bez połączenia przepadają; modele subskrybują ponownie po połączeniu
            std::lock_guard lock(m_mutex);
            m_queue.clear();
        }
        if (m_quit)
            break;
        io.provider->getTimer().afterDelay(1 * kj::SECONDS).exclusiveJoin(wakeObserver.whenBecomesReadable()).wait(io.waitScope);
        drainWake();
    }
}

// --- Subskrypcje ---

quint64 ServiceConnection::watchDirectory(const QString &path, QObject *receiver)
{
    const quint64 id = m_nextId++;
    m_directoryReceivers.insert(id, receiver);
    const QByteArray utf8 = path.toUtf8();
    post([this, id, utf8](KjContext &context) {
        auto request = context.service.watchDirectoryRequest();
        request.setPath(utf8.constData());
        request.setListener(kj::heap<DirectoryListenerImpl>(this, id));
        auto promise = request.send();
        // Uchwyt dostępny od razu (pipelining), bez czekania na odpowiedź
        context.handles.insert_or_assign(id, promise.getSubscription());
        context.tasks->add(promise.ignoreResult().catch_([this, id](kj::Exception &&exception) {
            DirectoryEvent event;
            event.type = DirectoryEvent::Failed;
            event.message = QString::fromUtf8(exception.getDescription().cStr());
            deliverDirectoryEvent(id, std::move(event));
        }));
    });
    return id;
}

quint64 ServiceConnection::search(const QString &path, const QString &query, QObject *receiver)
{
    const quint64 id = m_nextId++;
    m_directoryReceivers.insert(id, receiver);
    const QByteArray utf8Path = path.toUtf8();
    const QByteArray utf8Query = query.toUtf8();
    post([this, id, utf8Path, utf8Query](KjContext &context) {
        auto request = context.service.searchRequest();
        request.setPath(utf8Path.constData());
        request.setQuery(utf8Query.constData());
        request.setListener(kj::heap<DirectoryListenerImpl>(this, id));
        auto promise = request.send();
        context.handles.insert_or_assign(id, promise.getSubscription());
        context.tasks->add(promise.ignoreResult());
    });
    return id;
}

void ServiceConnection::unsubscribe(quint64 id)
{
    if (id == 0)
        return;
    m_directoryReceivers.remove(id);
    // Porzucenie uchwytu kończy subskrypcję po stronie usługi
    post([id](KjContext &context) { context.handles.erase(id); });
}

void ServiceConnection::deliverDirectoryEvent(quint64 id, DirectoryEvent event)
{
    toGui([this, id, event = std::move(event)] {
        QPointer<QObject> receiver = m_directoryReceivers.value(id);
        if (auto *target = dynamic_cast<DirectoryReceiver *>(receiver.data()))
            target->directoryEvent(event);
    });
}

// --- Zadania ---

quint64 ServiceConnection::trash(const QStringList &paths, std::function<void(const JobEvent &)> callback)
{
    const quint64 id = m_nextId++;
    m_jobCallbacks.insert(id, std::move(callback));
    post([this, id, paths](KjContext &context) {
        auto request = context.service.trashRequest();
        auto list = request.initPaths(paths.size());
        for (int i = 0; i < paths.size(); ++i)
            list.set(i, paths.at(i).toUtf8().constData());
        request.setListener(kj::heap<JobListenerImpl>(this, id));
        auto promise = request.send();
        context.handles.insert_or_assign(id, promise.getJob());
        context.tasks->add(promise.ignoreResult());
    });
    return id;
}

quint64 ServiceConnection::transfer(const QStringList &sources, const QString &destination, bool move,
                                    std::function<void(const JobEvent &)> callback)
{
    const quint64 id = m_nextId++;
    m_jobCallbacks.insert(id, std::move(callback));
    const QByteArray utf8Destination = destination.toUtf8();
    post([this, id, sources, utf8Destination, move](KjContext &context) {
        auto request = context.service.transferRequest();
        auto list = request.initSources(sources.size());
        for (int i = 0; i < sources.size(); ++i)
            list.set(i, sources.at(i).toUtf8().constData());
        request.setDestination(utf8Destination.constData());
        request.setMode(move ? TransferMode::MOVE : TransferMode::COPY);
        request.setListener(kj::heap<JobListenerImpl>(this, id));
        auto promise = request.send();
        context.handles.insert_or_assign(id, promise.getJob());
        context.tasks->add(promise.ignoreResult());
    });
    return id;
}

void ServiceConnection::cancelJob(quint64 id)
{
    post([id](KjContext &context) { context.handles.erase(id); });
}

void ServiceConnection::deliverJobEvent(quint64 id, JobEvent event)
{
    toGui([this, id, event = std::move(event)] {
        auto callback = m_jobCallbacks.value(id);
        if (callback)
            callback(event);
        if (event.finished) {
            m_jobCallbacks.remove(id);
            // Zadanie skończone: uchwyt nie jest już potrzebny
            post([id](KjContext &context) { context.handles.erase(id); });
        }
    });
}
