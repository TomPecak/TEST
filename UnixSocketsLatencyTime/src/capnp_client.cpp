// Klient Cap'n Proto RPC: pełne wywołanie zdalnej metody (serializacja, obietnice, pętla KJ).

#include "bench.h"
#include "latency.capnp.h"

#include <capnp/ez-rpc.h>

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "capnp-rpc");

    const std::string address = "unix:" + options.socketPath;
    capnp::EzRpcClient client(address.c_str());
    auto &waitScope = client.getWaitScope();
    PingService::Client pingService = client.getMain<PingService>();

    bench::Recorder recorder(options);
    for (uint64_t i = 0; i < static_cast<uint64_t>(recorder.totalIterations()); ++i) {
        auto request = pingService.pingRequest();
        request.setSeq(i);

        bench::pause(options);
        const int64_t start = bench::nowNs();
        auto response = request.send().wait(waitScope);
        const int64_t end = bench::nowNs();

        if (response.getSeq() != i) {
            std::fprintf(stderr, "zły numer: %lu zamiast %lu\n", response.getSeq(), i);
            return 1;
        }
        recorder.add(static_cast<long>(i), end - start);
    }

    recorder.report();
    return 0;
}
