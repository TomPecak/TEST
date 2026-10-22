// Klient na surowym gnieździe Unix z blokującym read(): najprostsza możliwa ścieżka,
// punkt odniesienia dla pozostałych wariantów.

#include "bench.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// Czyta dokładnie size bajtów (read może zwrócić mniej)
static bool readExactly(int fd, void *data, size_t size)
{
    char *bytes = static_cast<char *>(data);
    while (size > 0) {
        ssize_t read = ::read(fd, bytes, size);
        if (read <= 0)
            return false;
        bytes += read;
        size -= read;
    }
    return true;
}

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "c-blocking");

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, options.socketPath.c_str(), sizeof(address.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == -1) {
        perror("connect");
        return 1;
    }

    bench::Recorder recorder(options);
    for (uint64_t i = 0; i < static_cast<uint64_t>(recorder.totalIterations()); ++i) {
        bench::pause(options);
        const int64_t start = bench::nowNs();
        if (::write(fd, &i, sizeof(i)) != sizeof(i)) {
            perror("write");
            return 1;
        }
        uint64_t echoed = 0;
        if (!readExactly(fd, &echoed, sizeof(echoed))) {
            std::fprintf(stderr, "serwer zamknął połączenie\n");
            return 1;
        }
        const int64_t end = bench::nowNs();
        if (echoed != i) {
            std::fprintf(stderr, "zły numer: %lu zamiast %lu\n", echoed, i);
            return 1;
        }
        recorder.add(static_cast<long>(i), end - start);
    }

    close(fd);
    recorder.report();
    return 0;
}
