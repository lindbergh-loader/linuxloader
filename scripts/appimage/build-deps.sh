#!/usr/bin/env bash
set -euo pipefail

APPIMAGEDIRNAME=linuxloader.AppDir
OUTPUT_FOLDER=$(realpath "./$APPIMAGEDIRNAME")

export PKG_CONFIG_LIBDIR=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig

if [[ ! -f /usr/lib/i386-linux-gnu/libSDL3.a ]]; then
    sudo rm -f /usr/lib/i386-linux-gnu/cmake/SDL3/SDL3staticTargets*.cmake
fi

rm -rf build-deps
mkdir build-deps
cd build-deps

# Build SDL3
git clone --branch release-3.4.8 https://github.com/libsdl-org/SDL.git
cd SDL
mkdir build
cd build
cmake ../ -DCMAKE_C_FLAGS=-m32 -DCMAKE_INSTALL_PREFIX=/usr -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF
make -j4
sudo make install
cd ../../

# Build SDL3_ttf
git clone https://github.com/libsdl-org/SDL_ttf.git
cd SDL_ttf
mkdir build
cd build
cmake ../ -DCMAKE_C_FLAGS=-m32 -DCMAKE_INSTALL_PREFIX=/usr -DSDLTTF_HARFBUZZ=OFF
make -j4
sudo make install
cd ../../

# Build SDL3_image
git clone https://github.com/libsdl-org/SDL_image.git
cd SDL_image
mkdir build
cd build
cmake ../ -DCMAKE_C_FLAGS=-m32 -DCMAKE_INSTALL_PREFIX=/usr
make -j4
sudo make install
cd ../../

# Build libFAudio
git clone --branch 25.12 https://github.com/FNA-XNA/FAudio.git
cd FAudio
mkdir build
cd build
cmake ../ -DCMAKE_C_FLAGS=-m32 -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_SHARED_LIBS=off
make -j4
sudo make install
cd ../../../
