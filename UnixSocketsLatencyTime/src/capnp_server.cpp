// Serwer Cap'n Proto RPC: interfejs PingService z latency.capnp, EzRpcServer na gnieździe Unix.

#include "bench.h"
#include "latency.capnp.h"

#include <capnp/ez-rpc.h>
#include <unistd.h>

class PingServiceImpl final : public PingService::Server
{
public:
    kj::Promise<void> ping(PingContext context) override
    {
        // Odpowiedź budowana w miejscu, bez kopiowania
        context.getResults().setSeq(context.getParams().getSeq());
        return kj::READY_NOW;
    }
};

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "capnp");
    unlink(options.socketPath.c_str());

    // EzRpcServer sam tworzy gniazdo, epoll i pętlę zdarzeń KJ
    const std::string address = "unix:" + options.socketPath;
    capnp::EzRpcServer server(kj::heap<PingServiceImpl>(), address.c_str());
    std::printf("capnp_server: nasłuchuje na unix:%s\n", options.socketPath.c_str());
    std::fflush(stdout);

    kj::NEVER_DONE.wait(server.getWaitScope());
    return 0;
}
