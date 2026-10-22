// Natywny klient gRPC C++ (synchroniczny stub): koszt samego gRPC (HTTP/2, protobuf),
// bez Qt. Porównanie z qtgrpc_client pokazuje narzut QtGrpc i pętli zdarzeń Qt.

#include "bench.h"
#include "latency.grpc.pb.h"

#include <grpcpp/grpcpp.h>

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "grpc-cpp");

    auto channel = grpc::CreateChannel("unix:" + options.socketPath, grpc::InsecureChannelCredentials());
    auto stub = latency::PingService::NewStub(channel);

    bench::Recorder recorder(options);
    for (uint64_t i = 0; i < static_cast<uint64_t>(recorder.totalIterations()); ++i) {
        latency::PingMsg request;
        latency::PingMsg response;
        request.set_seq(i);
        grpc::ClientContext context; // kontekst jest jednorazowy, nowy dla każdego wywołania

        bench::pause(options);
        const int64_t start = bench::nowNs();
        const grpc::Status status = stub->ping(&context, request, &response);
        const int64_t end = bench::nowNs();

        if (!status.ok() || response.seq() != i) {
            std::fprintf(stderr, "błąd wywołania: %s\n", status.error_message().c_str());
            return 1;
        }
        recorder.add(static_cast<long>(i), end - start);
    }

    recorder.report();
    return 0;
}
