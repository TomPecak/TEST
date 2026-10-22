// Serwer gRPC (referencyjna biblioteka gRPC C++, serwer synchroniczny z pulą wątków),
// interfejs PingService z latency.proto, gniazdo Unix. Ten sam serwer dla obu klientów:
// grpc_client_cpp (natywny klient gRPC) i qtgrpc_client (QtGrpc).

#include "bench.h"
#include "latency.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <unistd.h>

class PingServiceImpl final : public latency::PingService::Service
{
    grpc::Status ping(grpc::ServerContext *, const latency::PingMsg *request, latency::PingMsg *response) override
    {
        response->set_seq(request->seq());
        return grpc::Status::OK;
    }
};

int main(int argc, char **argv)
{
    const bench::Options options = bench::parseOptions(argc, argv, "grpc");
    unlink(options.socketPath.c_str());

    PingServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + options.socketPath, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        std::fprintf(stderr, "nie udało się uruchomić serwera gRPC\n");
        return 1;
    }
    std::printf("grpc_server: nasłuchuje na unix:%s\n", options.socketPath.c_str());
    std::fflush(stdout);

    server->Wait();
    return 0;
}
