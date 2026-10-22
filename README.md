# TEST

## Run Qt gRPC examples, protobuf on linux
```bash
sudo apt update
sudo apt install protobuf-compiler libprotobuf-dev libgrpc++-dev protobuf-compiler-grpc

# nie wiem czy ponizsza komenda jest prawidlowa
sudo apt install -y libqt6protobuf6 libqt6protobufqtcoretypes6 libqt6protobufqtguitypes6 libqt6protobufquick6 libqt6protobufwellknowntypes6 qt6-grpc-tools

sudo apt install qt6-grpc-dev libprotobuf-dev protobuf-compiler
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
