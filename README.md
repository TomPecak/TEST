# TEST

## Run Qt gRPC examples, protobuf on linux
```bash
sudo apt update
sudo apt install protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc
```

## Run 
```bash
curl -O https://capnproto.org/capnproto-c++-1.5.0.tar.gz
tar zxf capnproto-c++-1.5.0.tar.gz
cd capnproto-c++-1.5.0
./configure
make -j6 check
sudo make install
```
