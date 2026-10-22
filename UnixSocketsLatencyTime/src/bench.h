#pragma once

// Wspólna część pomiaru dla wszystkich wariantów: opcje z linii poleceń, zegar i statystyki.
// Dzięki temu każdy wariant robi dokładnie tę samą próbę i drukuje wyniki w tym samym formacie.

#include <algorithm>
#include <chrono>
#include <clocale>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace bench {

struct Options {
    std::string socketPath;
    std::string name;
    long count = 100000; // liczba pomiarów do statystyk
    long warmup = 1000;  // pierwsze iteracje pomijane (rozgrzewka: cache, alokacje, taktowanie CPU)
    // Przerwa przed każdym wywołaniem; 0 = wywołania "na gorąco", jedno za drugim.
    // Przy przerwie procesor i wątki zdążą zasnąć: tak wyglądają rzadkie wywołania w shellu.
    // W mikrosekundach (--interval-us), żeby dało się zmierzyć próg zasypiania rdzenia (setki µs)
    long intervalUs = 0;
    // Dla klientów z QTimer (dokładność do 1 ms)
    long intervalMs() const { return intervalUs / 1000; }
};

// Opis przerwy do wydruku: "co 250 µs", "co 50 ms"
inline std::string intervalLabel(long intervalUs)
{
    if (intervalUs <= 0)
        return "na gorąco";
    if (intervalUs % 1000 == 0)
        return "co " + std::to_string(intervalUs / 1000) + " ms";
    return "co " + std::to_string(intervalUs) + " µs";
}

// --socket PATH  --count N  --warmup N  --interval-ms N | --interval-us N  --name NAZWA
inline Options parseOptions(int argc, char **argv, const char *defaultName)
{
    Options options;
    options.name = defaultName;
    const char *runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    options.socketPath = std::string(runtimeDir ? runtimeDir : "/tmp") + "/" + defaultName + ".sock";

    for (int i = 1; i < argc; ++i) {
        const bool hasValue = i + 1 < argc;
        if (!std::strcmp(argv[i], "--socket") && hasValue)
            options.socketPath = argv[++i];
        else if (!std::strcmp(argv[i], "--count") && hasValue)
            options.count = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--warmup") && hasValue)
            options.warmup = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--interval-ms") && hasValue)
            options.intervalUs = std::atol(argv[++i]) * 1000;
        else if (!std::strcmp(argv[i], "--interval-us") && hasValue)
            options.intervalUs = std::atol(argv[++i]);
        else if (!std::strcmp(argv[i], "--name") && hasValue)
            options.name = argv[++i];
        else {
            std::fprintf(stderr, "Użycie: %s [--socket PATH] [--count N] [--warmup N] [--interval-ms N | --interval-us N] [--name NAZWA]\n", argv[0]);
            std::exit(2);
        }
    }
    return options;
}

// Zegar monotoniczny (steady_clock gwarantuje monotoniczność, high_resolution_clock nie)
inline int64_t nowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Przerwa przed kolejnym wywołaniem w trybie rzadkich wywołań (dla klientów bez pętli zdarzeń)
inline void pause(const Options &options)
{
    if (options.intervalUs > 0)
        std::this_thread::sleep_for(std::chrono::microseconds(options.intervalUs));
}

// Zbiera czasy RTT; wyniki drukuje dopiero na końcu, żeby wypisywanie nie zakłócało pomiaru
class Recorder
{
public:
    explicit Recorder(const Options &options)
        : m_options(options)
    {
        m_samples.reserve(options.count);
    }

    long totalIterations() const { return m_options.warmup + m_options.count; }

    // iteration: numer iteracji liczony od zera, razem z rozgrzewką
    void add(long iteration, int64_t rttNs)
    {
        if (iteration >= m_options.warmup)
            m_samples.push_back(rttNs);
    }

    void report() const
    {
        if (m_samples.empty()) {
            std::fprintf(stderr, "[%s] brak pomiarów\n", m_options.name.c_str());
            return;
        }
        // Liczby zawsze z kropką (Qt ustawia locale z systemu, np. polskie z przecinkiem)
        std::setlocale(LC_NUMERIC, "C");
        std::vector<int64_t> sorted = m_samples;
        std::sort(sorted.begin(), sorted.end());
        const size_t n = sorted.size();
        auto percentile = [&](double p) {
            size_t index = static_cast<size_t>(p * n);
            return us(sorted[std::min(index, n - 1)]);
        };
        double sum = 0;
        for (int64_t v : sorted)
            sum += v;
        const double mean = sum / n / 1000.0;

        std::printf("[%s] %zu pomiarów (pominięto %ld na rozgrzewkę), %s, RTT w µs:\n",
                    m_options.name.c_str(), n, m_options.warmup,
                    intervalLabel(m_options.intervalUs).c_str());
        std::printf("  min %.2f | p50 %.2f | p90 %.2f | p99 %.2f | p99.9 %.2f | max %.2f | średnia %.2f\n",
                    us(sorted.front()), percentile(0.50), percentile(0.90), percentile(0.99),
                    percentile(0.999), us(sorted.back()), mean);
        // Linia do zbierania wyników przez skrypt
        std::printf("RESULT name=%s n=%zu min=%.3f p50=%.3f p90=%.3f p99=%.3f p999=%.3f max=%.3f mean=%.3f\n",
                    m_options.name.c_str(), n, us(sorted.front()), percentile(0.50), percentile(0.90),
                    percentile(0.99), percentile(0.999), us(sorted.back()), mean);
        std::fflush(stdout);
    }

private:
    static double us(int64_t ns) { return ns / 1000.0; }

    Options m_options;
    std::vector<int64_t> m_samples;
};

} // namespace bench
