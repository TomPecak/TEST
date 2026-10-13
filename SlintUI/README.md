sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
  libfontconfig1-dev libxcb-shape0-dev libxcb-xfixes0-dev \
  libxkbcommon-dev libinput-dev libgbm-dev
  
 
sudo apt install ninja-build 
 
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

source $HOME/.cargo/env


mkdir slint-cpp-demo
cd slint-cpp-demo
