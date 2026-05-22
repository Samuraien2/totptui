#!/bin/bash
if [[ "$(uname -s)" != "Linux" ]]; then
  echo "Must be compiled in Linux or WSL"
  exit 1
fi

cd "$(dirname "${BASH_SOURCE[0]}")"

if [ ! -f libcotp/CMakeLists.txt ]; then
  echo "dependency libcotp not pulled, try running: git submodule update --init --recursive"
  exit 1
fi

# Build libcotp dependency
cd libcotp
mkdir -p build
cd build
cmake ..
make
cd ..
cd ..

# Build our project
mkdir -p build
cp $(realpath libcotp/build/libcotp.so) build/libcotp.so.4

cc main.c -o build/totptui -lsqlite3 -l:libcotp.so.4 -Lbuild -Wl,-rpath,'$ORIGIN'

