// #include <QCoreApplication>
// #include <QLocalServer>
// #include <QLocalSocket>
// #include <QDebug>

// class EchoServer : public QObject {
//     Q_OBJECT
// public:
//     EchoServer(QObject *parent = nullptr) : QObject(parent) {
//         server = new QLocalServer(this);

//         const QString socketName = "latency_test_socket";

//         // Zawsze czyścimy pozostałości po poprzednim gnieździe przed nasłuchiwaniem
//         QLocalServer::removeServer(socketName);

//         if (!server->listen(socketName)) {
//             qCritical() << "Nie udało się uruchomić serwera:" << server->errorString();
//             exit(1);
//         }

//         qInfo() << "Serwer nasłuchuje na gnieździe:" << socketName;
//         connect(server, &QLocalServer::newConnection, this, &EchoServer::onNewConnection);
//     }

// private slots:
//     void onNewConnection() {
//         QLocalSocket *clientSocket = server->nextPendingConnection();
//         qInfo() << "Klient podłączony!";

//         connect(clientSocket, &QLocalSocket::readyRead, this, [clientSocket]() {
//             // Używamy bufora na stosie dla maksymalnej wydajności
//             // i zminimalizowania narzutu alokacji pamięci na pomiar latency.
//             char buffer[64];
//             qint64 bytesRead = clientSocket->read(buffer, sizeof(buffer));

//             if (bytesRead > 0) {
//                 clientSocket->write(buffer, bytesRead);
//                 clientSocket->flush(); // Natychmiastowe wysłanie do bufora jądra
//             }
//         });

//         connect(clientSocket, &QLocalSocket::disconnected, clientSocket, &QLocalSocket::deleteLater);
//     }

// private:
//     QLocalServer *server;
// };

// // Niezbędne, gdy klasa z Q_OBJECT znajduje się bezpośrednio w pliku .cpp
// #include "server.moc"

// int main(int argc, char *argv[]) {
//     QCoreApplication a(argc, argv);
//     EchoServer server;
//     return a.exec();
// }

//----------------------------------------------------------------------

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <sys/socket.h>
// #include <sys/un.h>
// #include <sys/epoll.h>

// // Qt QLocalServer pod Linuksem domyślnie tworzy gniazdo w katalogu /tmp/
// #define SOCKET_PATH "/tmp/latency_test_socket"
// #define MAX_EVENTS 10

// // Funkcja pomocnicza do ustawiania gniazda w tryb nieblokujący
// int set_nonblocking(int fd) {
//     int flags = fcntl(fd, F_GETFL, 0);
//     if (flags == -1) return -1;
//     return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
// }

// int main() {
//     printf("Start C Server");
//     fflush(stdout);

//     int server_fd, epoll_fd;
//     struct sockaddr_un server_addr;
//     struct epoll_event ev, events[MAX_EVENTS];

//     // 1. Tworzenie gniazda UNIX Domain Socket
//     server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
//     if (server_fd == -1) {
//         perror("Błąd socket()");
//         exit(EXIT_FAILURE);
//     }

//     // 2. Czyszczenie poprzedniego pliku gniazda (odpowiednik QLocalServer::removeServer)
//     unlink(SOCKET_PATH);

//     // 3. Bindowanie gniazda do ścieżki
//     memset(&server_addr, 0, sizeof(server_addr));
//     server_addr.sun_family = AF_UNIX;
//     strncpy(server_addr.sun_path, SOCKET_PATH, sizeof(server_addr.sun_path) - 1);

//     if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
//         perror("Błąd bind()");
//         close(server_fd);
//         exit(EXIT_FAILURE);
//     }

//     // 4. Nasłuchiwanie (odpowiednik server->listen)
//     if (listen(server_fd, SOMAXCONN) == -1) {
//         perror("Błąd listen()");
//         close(server_fd);
//         exit(EXIT_FAILURE);
//     }

//     printf("Serwer (C/epoll) nasłuchuje na gnieździe: %s\n", SOCKET_PATH);

//     // 5. Inicjalizacja instancji epoll
//     epoll_fd = epoll_create1(0);
//     if (epoll_fd == -1) {
//         perror("Błąd epoll_create1()");
//         close(server_fd);
//         exit(EXIT_FAILURE);
//     }

//     // Dodanie gniazda serwera do epoll
//     ev.events = EPOLLIN; // Zgłaszaj gdy ktoś chce się podłączyć
//     ev.data.fd = server_fd;
//     if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
//         perror("Błąd epoll_ctl: server_fd");
//         exit(EXIT_FAILURE);
//     }

//     // 6. Główna pętla zdarzeń (odpowiednik QCoreApplication::exec() + Event Loop)
//     while (1) {
//         int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1); // Czeka na zdarzenie (blokuje)
//         if (nfds == -1) {
//             if (errno == EINTR) continue; // Przerwane przez sygnał, kontynuuj
//             perror("Błąd epoll_wait()");
//             break;
//         }

//         for (int i = 0; i < nfds; ++i) {
//             if (events[i].data.fd == server_fd) {
//                 // ZDARZENIE: Nowe połączenie od klienta (odpowiednik onNewConnection)
//                 int client_fd = accept(server_fd, NULL, NULL);
//                 if (client_fd == -1) {
//                     perror("Błąd accept()");
//                     continue;
//                 }
//                 printf("Klient podłączony! (FD: %d)\n", client_fd);

//                 // Ustawiamy klienta jako nieblokującego
//                 set_nonblocking(client_fd);

//                 // Dodajemy klienta do epoll (EPOLLIN - dane do odczytu, EPOLLET - Edge Triggered)
//                 ev.events = EPOLLIN | EPOLLET;
//                 ev.data.fd = client_fd;
//                 if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
//                     perror("Błąd epoll_ctl: client_fd");
//                     close(client_fd);
//                 }
//             } else {
//                 // ZDARZENIE: Dane gotowe do odczytu (odpowiednik readyRead)
//                 int client_fd = events[i].data.fd;
//                 char buffer[64]; // Bufor na stosie

//                 // Czytamy w pętli (wymóg trybu EPOLLET)
//                 while (1) {
//                     ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

//                     if (bytes_read > 0) {
//                         // Od razu odsyłamy to, co przyszło (Echo)
//                         // Używamy write, dane od razu lądują w buforze jądra (brak potrzeby flush())
//                         ssize_t bytes_written = write(client_fd, buffer, bytes_read);
//                         if (bytes_written == -1) {
//                             perror("Błąd write()");
//                         }
//                     } else if (bytes_read == 0) {
//                         // EOF - Klient się rozłączył (odpowiednik disconnected)
//                         printf("Klient rozłączony (FD: %d)\n", client_fd);
//                         close(client_fd); // Zamknięcie fd automatycznie usuwa go z epoll
//                         break;
//                     } else {
//                         // bytes_read < 0
//                         if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                             // Przeczytano wszystkie dostępne dane z bufora, wracamy do epoll_wait
//                             break;
//                         } else {
//                             perror("Błąd read()");
//                             close(client_fd);
//                             break;
//                         }
//                     }
//                 }
//             }
//         }
//     }

//     // Sprzątanie po wyjściu
//     close(server_fd);
//     close(epoll_fd);
//     unlink(SOCKET_PATH);
//     return 0;
// }

//----------------------------- Cap'n Proto -----------------------------------

#include "latency.capnp.h"
#include <capnp/ez-rpc.h>
#include <kj/debug.h>
#include <iostream>
#include <unistd.h>

// Implementacja interfejsu wygenerowanego z pliku .capnp
class PingServiceImpl final : public PingService::Server {
public:
    // Każda metoda RPC w Cap'n Proto otrzymuje kontekst (Context)
    // Zawiera on Buildery do Requestu (Params) i Response'u (Results)
    kj::Promise<void> ping(PingContext context) override {
        // 1. Odczytujemy dane z requestu
        uint64_t seq = context.getParams().getSeq();

        // 2. Wpisujemy dane do odpowiedzi (Zero-Copy in-place build)
        context.getResults().setSeq(seq);

        // 3. Zwracamy gotową odpowiedź.
        // kj::READY_NOW oznacza, że nie potrzebujemy tu asynchronicznego czekania.
        return kj::READY_NOW;
    }
};

int main() {
    const char* socket_path = "/tmp/capnp_latency.sock";

    // Usuwamy stare gniazdo, jeśli istnieje
    unlink(socket_path);

    // Definiujemy adres jako UNIX socket (KJ automatycznie to parsuje!)
    std::string address = std::string("unix:") + socket_path;

    std::cout << "Serwer Cap'n Proto nasłuchuje na " << address << std::endl;

    // Magia EzRpcServer - automatycznie konfiguruje epoll, gniazdo i KJ Event Loop
    capnp::EzRpcServer server(address);

    // Eksponujemy naszą usługę
    server.exportCap("PingService", kj::heap<PingServiceImpl>());

    // Wchodzimy w nieskończoną pętlę zdarzeń (Event Loop)
    auto& waitScope = server.getWaitScope();
    kj::NEVER_DONE.wait(waitScope);

    return 0;
}