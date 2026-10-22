// Serwer echo na surowych gniazdach Unix i epoll (bez Qt, bez bibliotek).
// Odsyła dokładnie to, co dostał. Współpracuje z klientami: client_c_blocking, client_c_epoll, client_qt.

#include "bench.h"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int setNonBlocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "echo_c");
    std::signal(SIGPIPE, SIG_IGN);

    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, options.socketPath.c_str(), sizeof(address.sun_path) - 1);
    unlink(options.socketPath.c_str());

    if (bind(serverFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1 || listen(serverFd, SOMAXCONN) == -1) {
        perror("bind/listen");
        return 1;
    }

    int epollFd = epoll_create1(0);
    epoll_event event {};
    event.events = EPOLLIN;
    event.data.fd = serverFd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &event);

    std::printf("echo_server_c: nasłuchuje na %s\n", options.socketPath.c_str());
    std::fflush(stdout);

    epoll_event events[16];
    for (;;) {
        int count = epoll_wait(epollFd, events, 16, -1);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < count; ++i) {
            int fd = events[i].data.fd;
            if (fd == serverFd) {
                int clientFd = accept(serverFd, nullptr, nullptr);
                if (clientFd == -1)
                    continue;
                setNonBlocking(clientFd);
                epoll_event clientEvent {};
                clientEvent.events = EPOLLIN | EPOLLET;
                clientEvent.data.fd = clientFd;
                epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent);
                continue;
            }
            // Tryb edge-triggered: czytamy do wyczerpania danych
            char buffer[64];
            for (;;) {
                ssize_t read = ::read(fd, buffer, sizeof(buffer));
                if (read > 0) {
                    ssize_t offset = 0;
                    while (offset < read) {
                        ssize_t written = ::write(fd, buffer + offset, read - offset);
                        if (written <= 0)
                            break;
                        offset += written;
                    }
                } else if (read == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                    close(fd); // zamknięcie usuwa też deskryptor z epoll
                    break;
                } else {
                    break; // EAGAIN: wszystko przeczytane
                }
            }
        }
    }

    close(epollFd);
    close(serverFd);
    unlink(options.socketPath.c_str());
    return 0;
}
