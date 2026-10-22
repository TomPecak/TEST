#include "CalculatorBackend.h"

#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/async-unix.h>

#include <QClipboard>
#include <QDebug>
#include <QGuiApplication>
#include <QHash>
#include <QLocale>
#include <QQmlEngine>

#include <chrono>
#include <optional>

#include <sys/eventfd.h>
#include <unistd.h>

namespace {

CalculatorBackend *s_instance = nullptr;

QString toQString(capnp::Text::Reader text)
{
    return QString::fromUtf8(text.begin(), static_cast<qsizetype>(text.size()));
}

class LogErrors final : public kj::TaskSet::ErrorHandler
{
public:
    void taskFailed(kj::Exception &&exception) override
    {
        qWarning() << "[rpc]" << exception.getDescription().cStr();
    }
};

std::optional<Key> keyFromName(const QString &name)
{
    static const QHash<QString, Key> keys = {
        {QStringLiteral("0"), Key::DIGIT0}, {QStringLiteral("1"), Key::DIGIT1}, {QStringLiteral("2"), Key::DIGIT2},
        {QStringLiteral("3"), Key::DIGIT3}, {QStringLiteral("4"), Key::DIGIT4}, {QStringLiteral("5"), Key::DIGIT5},
        {QStringLiteral("6"), Key::DIGIT6}, {QStringLiteral("7"), Key::DIGIT7}, {QStringLiteral("8"), Key::DIGIT8},
        {QStringLiteral("9"), Key::DIGIT9}, {QStringLiteral("decimal"), Key::DECIMAL},
        {QStringLiteral("add"), Key::ADD}, {QStringLiteral("subtract"), Key::SUBTRACT},
        {QStringLiteral("multiply"), Key::MULTIPLY}, {QStringLiteral("divide"), Key::DIVIDE},
        {QStringLiteral("equals"), Key::EQUALS}, {QStringLiteral("percent"), Key::PERCENT},
        {QStringLiteral("sqrt"), Key::SQUARE_ROOT}, {QStringLiteral("square"), Key::SQUARE},
        {QStringLiteral("reciprocal"), Key::RECIPROCAL}, {QStringLiteral("negate"), Key::NEGATE},
        {QStringLiteral("ce"), Key::CLEAR_ENTRY}, {QStringLiteral("c"), Key::CLEAR},
        {QStringLiteral("backspace"), Key::BACKSPACE}, {QStringLiteral("mc"), Key::MEMORY_CLEAR},
        {QStringLiteral("mr"), Key::MEMORY_RECALL}, {QStringLiteral("m+"), Key::MEMORY_ADD},
        {QStringLiteral("m-"), Key::MEMORY_SUBTRACT}, {QStringLiteral("ms"), Key::MEMORY_STORE},
    };
    auto it = keys.constFind(name);
    if (it == keys.cend())
        return std::nullopt;
    return it.value();
}

double elapsedMs(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

// --- Słuchacze historii i pamięci (wywoływani przez usługę w wątku KJ) ---

class HistoryListenerImpl final : public HistoryListener::Server
{
public:
    explicit HistoryListenerImpl(CalculatorBackend *backend)
        : m_backend(backend)
    {
    }

protected:
    kj::Promise<void> changed(ChangedContext context) override
    {
        QVariantList items;
        for (auto item : context.getParams().getItems()) {
            items.append(QVariantMap{
                {QStringLiteral("id"), double(item.getId())},
                {QStringLiteral("expression"), toQString(item.getExpression())},
                {QStringLiteral("result"), toQString(item.getResult())},
            });
        }
        m_backend->toGui([backend = m_backend, items] {
            backend->m_history = items;
            emit backend->historyChanged();
        });
        return kj::READY_NOW;
    }

private:
    CalculatorBackend *m_backend;
};

class MemoryListenerImpl final : public MemoryListener::Server
{
public:
    explicit MemoryListenerImpl(CalculatorBackend *backend)
        : m_backend(backend)
    {
    }

protected:
    kj::Promise<void> changed(ChangedContext context) override
    {
        QVariantList items;
        for (auto item : context.getParams().getItems())
            items.append(toQString(item.getValue()));
        m_backend->toGui([backend = m_backend, items] {
            backend->m_memory = items;
            emit backend->memoryChanged();
        });
        return kj::READY_NOW;
    }

private:
    CalculatorBackend *m_backend;
};

// --- Obiekt ---

CalculatorBackend::CalculatorBackend(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_socketPath(socketPath)
{
    s_instance = this;
    // Liczby formatuje usługa, według ustawień regionalnych użytkownika
    const QLocale locale;
    m_decimal = locale.decimalPoint().toUtf8();
    m_group = locale.groupSeparator().toUtf8();
    m_eventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    m_thread = std::thread([this] { run(); });
}

CalculatorBackend::~CalculatorBackend()
{
    m_quit = true;
    wake();
    m_thread.join();
    close(m_eventFd);
    s_instance = nullptr;
}

CalculatorBackend *CalculatorBackend::instance()
{
    return s_instance;
}

CalculatorBackend *CalculatorBackend::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void CalculatorBackend::wake()
{
    const uint64_t one = 1;
    [[maybe_unused]] auto written = ::write(m_eventFd, &one, sizeof(one));
}

void CalculatorBackend::post(Task task)
{
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(task));
    }
    wake();
}

void CalculatorBackend::setConnected(bool connected)
{
    toGui([this, connected] {
        if (m_connected == connected)
            return;
        m_connected = connected;
        if (!connected) {
            // Nowa sesja po ponownym połączeniu zaczyna od zera
            m_display = QStringLiteral("0");
            m_expression.clear();
            m_error = false;
            emit stateChanged();
        }
        emit connectedChanged();
    });
}

void CalculatorBackend::deliverState(DisplayState::Reader state, double ms)
{
    const QString display = toQString(state.getDisplay());
    const QString expression = toQString(state.getExpression());
    const bool error = state.getError();
    toGui([this, display, expression, error, ms] {
        m_display = display;
        m_expression = expression;
        m_error = error;
        if (ms >= 0)
            m_lastCallMs = ms;
        emit stateChanged();
    });
}

// --- Wątek RPC ---

void CalculatorBackend::run()
{
    kj::AsyncIoContext io = kj::setupAsyncIo();
    kj::UnixEventPort::FdObserver wakeObserver(io.unixEventPort, m_eventFd, kj::UnixEventPort::FdObserver::OBSERVE_READ);
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

            KjContext context{client.bootstrap().castAs<CalculatorService>()};
            kj::TaskSet tasks(errors);
            context.tasks = &tasks;

            auto setFormat = [this](NumberFormat::Builder format) {
                format.setDecimal(m_decimal.constData());
                format.setGroup(m_group.constData());
            };

            // Sesja: uchwyt dostępny od razu (pipelining), bez czekania na odpowiedź
            auto open = context.service.openSessionRequest();
            setFormat(open.initFormat());
            context.session = open.send().getSession();

            auto history = context.service.watchHistoryRequest();
            setFormat(history.initFormat());
            history.setListener(kj::heap<HistoryListenerImpl>(this));
            auto historyPromise = history.send();
            context.subscriptions.push_back(historyPromise.getSubscription());
            tasks.add(historyPromise.ignoreResult());

            auto memory = context.service.watchMemoryRequest();
            setFormat(memory.initFormat());
            memory.setListener(kj::heap<MemoryListenerImpl>(this));
            auto memoryPromise = memory.send();
            context.subscriptions.push_back(memoryPromise.getSubscription());
            tasks.add(memoryPromise.ignoreResult());

            tasks.add(context.session.stateRequest().send().then([this](auto &&response) {
                deliverState(response.getState(), -1);
            }));

            bool disconnected = false;
            auto disconnect = client.onDisconnect()
                                  .then([&] { disconnected = true; }, [&](kj::Exception &&) { disconnected = true; })
                                  .fork();

            wasConnected = true;
            reportedFailure = false;
            setConnected(true);
            qInfo() << "[rpc] połączono z" << m_socketPath;

            while (!m_quit && !disconnected) {
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
            std::lock_guard lock(m_mutex);
            m_queue.clear();
        }
        if (m_quit)
            break;
        io.provider->getTimer().afterDelay(1 * kj::SECONDS).exclusiveJoin(wakeObserver.whenBecomesReadable()).wait(io.waitScope);
        drainWake();
    }
}

// --- Wywołania z QML ---

void CalculatorBackend::press(const QString &name)
{
    const auto key = keyFromName(name);
    if (!key || !m_connected)
        return;
    post([this, key = *key](KjContext &context) {
        auto request = context.session.pressRequest();
        request.setKey(key);
        const auto start = std::chrono::steady_clock::now();
        context.tasks->add(request.send().then([this, start](auto &&response) {
            deliverState(response.getState(), elapsedMs(start));
        }));
    });
}

void CalculatorBackend::recallHistory(double id)
{
    if (!m_connected)
        return;
    post([this, id = quint64(id)](KjContext &context) {
        auto request = context.session.recallHistoryRequest();
        request.setId(id);
        context.tasks->add(request.send().then([this](auto &&response) { deliverState(response.getState(), -1); }));
    });
}

void CalculatorBackend::memoryItem(int index, const QString &action)
{
    static const QHash<QString, MemoryAction> actions = {
        {QStringLiteral("recall"), MemoryAction::RECALL},
        {QStringLiteral("add"), MemoryAction::ADD},
        {QStringLiteral("subtract"), MemoryAction::SUBTRACT},
        {QStringLiteral("clear"), MemoryAction::CLEAR},
    };
    if (!m_connected || index < 0 || !actions.contains(action))
        return;
    post([this, index, memoryAction = actions.value(action)](KjContext &context) {
        auto request = context.session.memoryItemRequest();
        request.setIndex(uint32_t(index));
        request.setAction(memoryAction);
        context.tasks->add(request.send().then([this](auto &&response) { deliverState(response.getState(), -1); }));
    });
}

void CalculatorBackend::clearHistory()
{
    post([](KjContext &context) { context.tasks->add(context.service.clearHistoryRequest().send().ignoreResult()); });
}

void CalculatorBackend::clearMemory()
{
    post([](KjContext &context) { context.tasks->add(context.service.clearMemoryRequest().send().ignoreResult()); });
}

void CalculatorBackend::copy()
{
    if (m_error)
        return;
    // Bez separatorów tysięcy, z separatorem dziesiętnym użytkownika (jak kalkulator Windows)
    QString text = m_display;
    const QString group = QString::fromUtf8(m_group);
    if (!group.isEmpty())
        text.remove(group);
    QGuiApplication::clipboard()->setText(text);
}

void CalculatorBackend::paste()
{
    const QByteArray text = QGuiApplication::clipboard()->text().trimmed().toUtf8();
    if (text.isEmpty() || !m_connected)
        return;
    post([this, text](KjContext &context) {
        auto request = context.session.pasteRequest();
        request.setText(text.constData());
        context.tasks->add(request.send().then([this](auto &&response) {
            if (!response.getOk())
                toGui([this] { emit pasteRejected(); });
            deliverState(response.getState(), -1);
        }));
    });
}

void CalculatorBackend::evaluate(const QString &expression, const QJSValue &callback)
{
    const quint64 id = m_nextCallback++;
    m_callbacks.insert(id, callback);
    if (!m_connected) {
        m_callbacks.take(id).call({false, QString(), tr("The calculator service is not running")});
        return;
    }
    const QByteArray text = expression.toUtf8();
    post([this, id, text](KjContext &context) {
        auto request = context.service.evaluateRequest();
        request.setExpression(text.constData());
        auto format = request.initFormat();
        format.setDecimal(m_decimal.constData());
        format.setGroup(m_group.constData());
        context.tasks->add(request.send().then([this, id](auto &&response) {
            const bool ok = response.getOk();
            const QString result = toQString(response.getResult());
            const QString message = toQString(response.getMessage());
            toGui([this, id, ok, result, message] {
                QJSValue callback = m_callbacks.take(id);
                if (callback.isCallable())
                    callback.call({ok, result, message});
            });
        }));
    });
}
