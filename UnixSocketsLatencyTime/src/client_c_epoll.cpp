// Klient na surowym gnieździe Unix w trybie nieblokującym z epoll: tak działa pod spodem
// pętla zdarzeń (Qt, KJ), ale bez żadnego narzutu biblioteki.

#include "bench.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// 8 bajtów zawsze mieści się w buforze gniazda, więc zapis nie zwraca EAGAIN
static void sendSequence(int fd, uint64_t sequence)
{
    if (::write(fd, &sequence, sizeof(sequence)) != sizeof(sequence)) {
        perror("write");
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "c-epoll");

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, options.socketPath.c_str(), sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1) {
        perror("connect");
        return 1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    int epollFd = epoll_create1(0);
    epoll_event event {};
    event.events = EPOLLIN;
    event.data.fd = fd;
    epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &event);

    bench::Recorder recorder(options);
    const uint64_t total = static_cast<uint64_t>(recorder.totalIterations());

    uint64_t sequence = 0;
    uint64_t echoed = 0;
    size_t received = 0; // ile bajtów bieżącej odpowiedzi już przyszło
    bench::pause(options);
    int64_t start = bench::nowNs();
    sendSequence(fd, sequence);

    epoll_event events[1];
    while (sequence < total) {
        int count = epoll_wait(epollFd, events, 1, -1);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            return 1;
        }
        ssize_t read = ::read(fd, reinterpret_cast<char *>(&echoed) + received, sizeof(echoed) - received);
        if (read == 0) {
            std::fprintf(stderr, "serwer zamknął połączenie\n");
            return 1;
        }
        if (read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            perror("read");
            return 1;
        }
        received += read;
        if (received < sizeof(echoed))
            continue;

        const int64_t end = bench::nowNs();
        if (echoed != sequence) {
            std::fprintf(stderr, "zły numer: %lu zamiast %lu\n", echoed, sequence);
            return 1;
        }
        recorder.add(static_cast<long>(sequence), end - start);
        received = 0;

        if (++sequence < total) {
            bench::pause(options);
            start = bench::nowNs();
            sendSequence(fd, sequence);
        }
    }

    close(epollFd);
    close(fd);
    recorder.report();
    return 0;
}
