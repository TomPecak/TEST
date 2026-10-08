// #include <QCoreApplication>
// #include <QLocalSocket>
// #include <QElapsedTimer>
// #include <QDebug>
// #include <QTimer>

// class LatencyClient : public QObject {
//     Q_OBJECT
// public:
//     LatencyClient(QObject *parent = nullptr) : QObject(parent), pingCount(0), totalLatencyNs(0) {
//         socket = new QLocalSocket(this);

//         connect(socket, &QLocalSocket::connected, this, &LatencyClient::onConnected);
//         connect(socket, &QLocalSocket::readyRead, this, &LatencyClient::onReadyRead);
//         connect(socket, &QLocalSocket::errorOccurred, this, [](QLocalSocket::LocalSocketError error) {
//             qCritical() << "Błąd gniazda:" << error;
//         });

//         qInfo() << "Łączenie z serwerem...";
//         socket->connectToServer("latency_test_socket");
//     }

// private slots:
//     void onConnected() {
//         qInfo() << "Połączono. Rozpoczynam pomiar latency...";
//         sendPing();
//     }

//     void onReadyRead() {
//         // Czekamy na pełne 8 bajtów (sizeof(quint64))
//         if (socket->bytesAvailable() < (qint64)sizeof(quint64))
//             return;

//         // Zatrzymanie pomiaru czasu jak najszybciej
//         qint64 latencyNs = timer.nsecsElapsed();

//         quint64 receivedSeq;
//         socket->read(reinterpret_cast<char*>(&receivedSeq), sizeof(receivedSeq));

//         qInfo() << "Odebrano nr:" << receivedSeq
//                 << "| RTT Latency:" << latencyNs / 1000.0 << "us";

//         // Nie bierzemy pod uwagę pierwszej iteracji do średniej (tzw. warm-up)
//         if (pingCount > 0) {
//             totalLatencyNs += latencyNs;
//         }

//         pingCount++;

//         if (pingCount < MAX_PINGS) {
//             // Wysyłamy następny pakiet ping-pong
//             sendPing();
//         } else {
//             qInfo() << "--- Test zakończony ---";
//             qInfo() << "Średnie RTT (bez pierwszej iteracji):"
//                     << (totalLatencyNs / (MAX_PINGS - 1)) / 1000.0 << "us";
//             QCoreApplication::quit();
//         }
//     }

//     void sendPing() {
//         // Zapisujemy bezpośrednio pamięć licznika bez obudowywania w QDataStream,
//         // aby uciąć narzut kodowania.
//         timer.start();
//         socket->write(reinterpret_cast<const char*>(&pingCount), sizeof(pingCount));
//         socket->flush(); // Natychmiastowe wypchnięcie danych do kernela
//     }

// private:
//     QLocalSocket *socket;
//     QElapsedTimer timer;
//     quint64 pingCount;
//     qint64 totalLatencyNs;
//     const quint64 MAX_PINGS = 1000; // Ilość pakietów do przetestowania
// };

// // Niezbędne, gdy klasa z Q_OBJECT znajduje się bezpośrednio w pliku .cpp
// #include "client.moc"

// int main(int argc, char *argv[]) {
//     QCoreApplication a(argc, argv);
//     LatencyClient client;
//     return a.exec();
// }

//-------------------------------------------------------------------------------------------

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <sys/socket.h>
// #include <sys/un.h>
// #include <time.h>
// #include <stdint.h>

// #define SOCKET_PATH "/tmp/latency_test_socket"
// #define MAX_PINGS 1000

// // Funkcja pomocnicza do obliczania różnicy czasu w nanosekundach
// int64_t get_elapsed_ns(struct timespec *start, struct timespec *end) {
//     int64_t start_ns = (int64_t)start->tv_sec * 1000000000LL + start->tv_nsec;
//     int64_t end_ns = (int64_t)end->tv_sec * 1000000000LL + end->tv_nsec;
//     return end_ns - start_ns;
// }

// int main() {
//     int sockfd;
//     struct sockaddr_un server_addr;

//     // 1. Tworzenie gniazda
//     sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
//     if (sockfd == -1) {
//         perror("Błąd tworzenia gniazda");
//         exit(EXIT_FAILURE);
//     }

//     // 2. Łączenie z serwerem
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sun_family = AF_UNIX;
//     strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

//     printf("Łączenie z serwerem...\n");
//     if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
//         perror("Błąd łączenia z serwerem");
//         close(sockfd);
//         exit(EXIT_FAILURE);
//     }
//     printf("Połączono. Rozpoczynam pomiar latency...\n");

//     // Zmienne do pomiaru
//     uint64_t pingCount = 0;
//     int64_t totalLatencyNs = 0;
//     struct timespec start_time, end_time;

//     // Główna pętla ping-pong
//     for (pingCount = 0; pingCount < MAX_PINGS; pingCount++) {

//         // --- START ZEGARA ---
//         clock_gettime(CLOCK_MONOTONIC, &start_time);

//         // 1. Wysłanie numeru sekwencyjnego
//         ssize_t bytes_written = write(sockfd, &pingCount, sizeof(pingCount));
//         if (bytes_written != sizeof(pingCount)) {
//             perror("Błąd wysyłania danych");
//             break;
//         }

//         // 2. Oczekiwanie na odpowiedź (wywołanie systemowe blokuje wątek do czasu nadejścia danych)
//         uint64_t receivedSeq;
//         ssize_t bytes_read = read(sockfd, &receivedSeq, sizeof(receivedSeq));

//         // --- STOP ZEGARA (Natychmiast po odczycie) ---
//         clock_gettime(CLOCK_MONOTONIC, &end_time);

//         if (bytes_read <= 0) {
//             printf("Serwer zamknął połączenie lub wystąpił błąd.\n");
//             break;
//         }

//         // 3. Obliczenia i logowanie
//         int64_t latencyNs = get_elapsed_ns(&start_time, &end_time);
//         double latencyUs = latencyNs / 1000.0;

//         printf("Odebrano nr: %lu | RTT Latency: %.3f us\n", receivedSeq, latencyUs);

//         // Omijamy pierwszą iterację do średniej (warm-up)
//         if (pingCount > 0) {
//             totalLatencyNs += latencyNs;
//         }
//     }

//     // 4. Podsumowanie
//     if (pingCount > 1) {
//         printf("--- Test zakończony ---\n");
//         double avgLatencyUs = (totalLatencyNs / (double)(MAX_PINGS - 1)) / 1000.0;
//         printf("Średnie RTT (bez pierwszej iteracji): %.3f us\n", avgLatencyUs);
//     }

//     close(sockfd);
//     return 0;
// }

//-----------------------------------------------------------------------------------------

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <sys/socket.h>
// #include <sys/un.h>
// #include <sys/epoll.h>
// #include <time.h>
// #include <stdint.h>

// #define SOCKET_PATH "/tmp/latency_test_socket"
// #define MAX_PINGS 1000
// #define MAX_EVENTS 1

// // Funkcja pomocnicza do obliczania różnicy czasu w nanosekundach
// int64_t get_elapsed_ns(struct timespec *start, struct timespec *end) {
//     int64_t start_ns = (int64_t)start->tv_sec * 1000000000LL + start->tv_nsec;
//     int64_t end_ns = (int64_t)end->tv_sec * 1000000000LL + end->tv_nsec;
//     return end_ns - start_ns;
// }

// // Funkcja do ustawiania gniazda w tryb nieblokujący
// int set_nonblocking(int fd) {
//     int flags = fcntl(fd, F_GETFL, 0);
//     if (flags == -1) return -1;
//     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// }

// int main() {
//     int sockfd, epoll_fd;
//     struct sockaddr_un server_addr;
//     struct epoll_event ev, events[MAX_EVENTS];

//     // 1. Tworzenie gniazda
//     sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
//     if (sockfd == -1) {
//         perror("Błąd tworzenia gniazda");
//         exit(EXIT_FAILURE);
//     }

//     // 2. Łączenie z serwerem
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sun_family = AF_UNIX;
//     strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

//     printf("Łączenie z serwerem...\n");
//     if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
//         perror("Błąd łączenia z serwerem");
//         close(sockfd);
//         exit(EXIT_FAILURE);
//     }
//     printf("Połączono.\n");

//     // UWAGA: Przełączamy gniazdo w tryb nieblokujący (to samo robi pod spodem QLocalSocket)
//     if (set_nonblocking(sockfd) == -1) {
//         perror("Błąd fcntl");
//         exit(EXIT_FAILURE);
//     }

//     // 3. Konfiguracja epoll
//     epoll_fd = epoll_create1(0);
//     if (epoll_fd == -1) {
//         perror("Błąd epoll_create1");
//         exit(EXIT_FAILURE);
//     }

//     // Chcemy być informowani, gdy pojawią się dane do odczytu (EPOLLIN)
//     ev.events = EPOLLIN;
//     ev.data.fd = sockfd;
//     if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
//         perror("Błąd epoll_ctl");
//         exit(EXIT_FAILURE);
//     }

//     // Zmienne do pomiaru
//     uint64_t pingCount = 0;
//     int64_t totalLatencyNs = 0;
//     struct timespec start_time, end_time;

//     printf("Rozpoczynam pomiar latency (tryb epoll)...\n");

//     // --- KICKSTART: Wysłanie pierwszej wiadomości przed wejściem w pętlę zdarzeń ---
//     clock_gettime(CLOCK_MONOTONIC, &start_time);
//     write(sockfd, &pingCount, sizeof(pingCount));

//     // 4. Główna pętla zdarzeń (Nasza prosta "Event Loop")
//     while (pingCount < MAX_PINGS) {
//         // Czekamy na zdarzenie od systemu operacyjnego (blokuje w epoll_wait)
//         int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

//         if (nfds == -1) {
//             if (errno == EINTR) continue; // Przerwane przez sygnał
//             perror("Błąd epoll_wait");
//             break;
//         }

//         for (int i = 0; i < nfds; i++) {
//             if (events[i].events & EPOLLIN) {
//                 // Mamy dane! Szybko czytamy i zatrzymujemy stoper
//                 uint64_t receivedSeq;
//                 ssize_t bytes_read = read(sockfd, &receivedSeq, sizeof(receivedSeq));

//                 clock_gettime(CLOCK_MONOTONIC, &end_time);

//                 if (bytes_read > 0) {
//                     // 5. Obliczenia i logowanie
//                     int64_t latencyNs = get_elapsed_ns(&start_time, &end_time);
//                     double latencyUs = latencyNs / 1000.0;

//                     printf("Odebrano nr: %lu | RTT Latency: %.3f us\n", receivedSeq, latencyUs);

//                     if (pingCount > 0) {
//                         totalLatencyNs += latencyNs;
//                     }

//                     pingCount++;

//                     // 6. Wysłanie kolejnego pinga
//                     if (pingCount < MAX_PINGS) {
//                         clock_gettime(CLOCK_MONOTONIC, &start_time);
//                         // Zapis do gniazda. Zakładamy że bufor się nie zapcha przy 8 bajtach (EAGAIN nie wystąpi)
//                         write(sockfd, &pingCount, sizeof(pingCount));
//                     }
//                 } else if (bytes_read == 0) {
//                     printf("Serwer zamknął połączenie.\n");
//                     pingCount = MAX_PINGS; // Wymuszenie wyjścia z pętli
//                 } else {
//                     if (errno != EAGAIN && errno != EWOULDBLOCK) {
//                         perror("Błąd odczytu");
//                         pingCount = MAX_PINGS;
//                     }
//                 }
//             }
//         }
//     }

//     // 7. Podsumowanie
//     if (pingCount > 1) {
//         printf("--- Test zakończony ---\n");
//         double avgLatencyUs = (totalLatencyNs / (double)(MAX_PINGS - 1)) / 1000.0;
//         printf("Średnie RTT (bez pierwszej iteracji): %.3f us\n", avgLatencyUs);
//     }

//     close(epoll_fd);
//     close(sockfd);
//     return 0;
// }


//----------------------------------------- Cap'n Proto --------------------------

#include "latency.capnp.h"
#include <capnp/ez-rpc.h>
#include <iostream>
#include <chrono>

int main() {
    const char* address = "unix:/tmp/capnp_latency.sock";
    std::cout << "Łączenie z serwerem Cap'n Proto na " << address << "..." << std::endl;

    // EzRpcClient automatycznie łączy się z podanym gniazdem UNIX / TCP
    capnp::EzRpcClient client(address);
    auto& waitScope = client.getWaitScope();

    // Pobieramy "zdolność" (Capability) do wywoływania naszej usługi
    PingService::Client pingService = client.importCap<PingService>("PingService");

    const int MAX_PINGS = 100000;
    long long totalLatencyNs = 0;

    std::cout << "Połączono. Rozpoczynam pomiar latency..." << std::endl;

    for (int i = 0; i < MAX_PINGS; ++i) {
        // 1. Inicjujemy żądanie (Zaalokowanie wiadomości Zero-Copy)
        auto request = pingService.pingRequest();

        // 2. Wypełniamy żądanie (Builder)
        request.setSeq(i);

        auto start = std::chrono::high_resolution_clock::now();

        // 3. Wysyłamy żądanie. Zwraca Obietnicę (Promise).
        auto promise = request.send();

        // 4. Czekamy na odpowiedź (synchroniczny blok na waitScope)
        auto response = promise.wait(waitScope);

        auto end = std::chrono::high_resolution_clock::now();

        // Odbieramy dane
        uint64_t receivedSeq = response.getSeq();

        auto latencyNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double latencyUs = latencyNs / 1000.0;

        std::cout << "Odebrano nr: " << receivedSeq << " | RTT Latency: " << latencyUs << " us\n";

        if (i > 0) { // Omijamy pierwszą iterację
            totalLatencyNs += latencyNs;
        }
    }

    std::cout << "--- Test zakończony ---\n";
    std::cout << "Średnie RTT (bez pierwszej iteracji): "
              << (totalLatencyNs / (double)(MAX_PINGS - 1)) / 1000.0 << " us\n";

    return 0;
}
