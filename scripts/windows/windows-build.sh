make win32 -j$(nproc)

mkdir win-release
cp build-win32/linuxloader.exe win-release/linuxloader.exe
#cp libs/win32/SDL3.dll win-release/SDL3.dll
cp -r libs/win32/ll-deps win-release/
cp /usr/lib/gcc/i686-w64-mingw32/13-win32/libgcc_s_dw2-1.dll win-release/ll-deps/

cd win-release
zip -r ../linuxloader-win32.zip .
cd ..
