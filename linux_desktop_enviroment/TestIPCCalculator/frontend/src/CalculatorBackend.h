#pragma once

// Kalkulator po stronie Qt (singleton QML "Calc"): przekazuje klawisze do usługi, pokazuje jej stan.
//
// Cała logika i liczenie są w usłudze (Rust); tu jest tylko połączenie Cap'n Proto w osobnym wątku
// (pętla KJ), tak jak w menedżerze plików. Wątek GUI nigdy nie czeka: klawisze trafiają do kolejki
// (eventfd budzi wątek KJ), odpowiedzi wracają sygnałami kolejkowymi. Odpowiedzi jednej sesji
// przychodzą w kolejności wysłania (E-order), więc szybkie pisanie nie miesza wyników.

#include "calculator.capnp.h"

#include <QJSValue>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace kj {
class TaskSet;
}

class QQmlEngine;
class QJSEngine;

// Stan dostępny tylko w wątku KJ
struct KjContext {
    CalculatorService::Client service;
    Session::Client session = nullptr;
    std::vector<capnp::Capability::Client> subscriptions; // historia, pamięć
    kj::TaskSet *tasks = nullptr;
};

class CalculatorBackend : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Calc)
    QML_SINGLETON

    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString socketPath READ socketPath CONSTANT)
    Q_PROPERTY(QString display READ display NOTIFY stateChanged)
    Q_PROPERTY(QString expression READ expression NOTIFY stateChanged)
    Q_PROPERTY(bool error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QVariantList memory READ memory NOTIFY memoryChanged)
    // Czas ostatniego naciśnięcia: od wysłania do odpowiedzi (wątek RPC), w ms
    Q_PROPERTY(double lastCallMs READ lastCallMs NOTIFY stateChanged)
    // Opcje startowe z wiersza poleceń: { selftest, screenshot }
    Q_PROPERTY(QVariantMap startOptions READ startOptions CONSTANT)

public:
    explicit CalculatorBackend(const QString &socketPath, QObject *parent = nullptr);
    ~CalculatorBackend() override;

    static CalculatorBackend *instance();
    static CalculatorBackend *create(QQmlEngine *, QJSEngine *);

    bool connected() const { return m_connected; }
    QString socketPath() const { return m_socketPath; }
    QString display() const { return m_display; }
    QString expression() const { return m_expression; }
    bool error() const { return m_error; }
    QVariantList history() const { return m_history; }
    QVariantList memory() const { return m_memory; }
    double lastCallMs() const { return m_lastCallMs; }
    QVariantMap startOptions() const { return m_startOptions; }
    void setStartOptions(const QVariantMap &options) { m_startOptions = options; }

    // Klawisz po nazwie: "0".."9", "decimal", "add", "subtract", "multiply", "divide", "equals",
    // "percent", "sqrt", "square", "reciprocal", "negate", "ce", "c", "backspace",
    // "mc", "mr", "m+", "m-", "ms"
    Q_INVOKABLE void press(const QString &key);
    Q_INVOKABLE void recallHistory(double id);
    // action: "recall", "add", "subtract", "clear"
    Q_INVOKABLE void memoryItem(int index, const QString &action);
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void clearMemory();
    Q_INVOKABLE void copy();
    Q_INVOKABLE void paste();
    // callback(ok, result, message): wyrażenie z kolejnością działań (dla testów i widżetów)
    Q_INVOKABLE void evaluate(const QString &expression, const QJSValue &callback);

signals:
    void connectedChanged();
    void stateChanged();
    void historyChanged();
    void memoryChanged();
    // Wklejony tekst nie był liczbą
    void pasteRejected();

private:
    using Task = std::function<void(KjContext &)>;

    void run();
    void post(Task task);
    void wake();
    void setConnected(bool connected);
    void deliverState(DisplayState::Reader state, double ms);
    template<typename F>
    void toGui(F f)
    {
        QMetaObject::invokeMethod(this, std::move(f), Qt::QueuedConnection);
    }

    friend class HistoryListenerImpl;
    friend class MemoryListenerImpl;

    QString m_socketPath;
    QByteArray m_decimal;
    QByteArray m_group;
    QVariantMap m_startOptions;

    // Tylko wątek GUI
    bool m_connected = false;
    QString m_display = QStringLiteral("0");
    QString m_expression;
    bool m_error = false;
    QVariantList m_history;
    QVariantList m_memory;
    double m_lastCallMs = 0;
    QHash<quint64, QJSValue> m_callbacks;
    quint64 m_nextCallback = 1;

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    int m_eventFd = -1;
    std::mutex m_mutex;
    std::deque<Task> m_queue;
};
