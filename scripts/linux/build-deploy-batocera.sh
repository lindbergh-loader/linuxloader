#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
batocera_repo=${BATOCERA_REPO:-/home/aderumier/rgs/Batocera}
batocera_output=${BATOCERA_OUTPUT:-/home/aderumier/code/batocera.linuxv43/output}
batocera_host=${BATOCERA_HOST:-batocera}
loader_dir="$batocera_repo/system/rgs/emulators/linuxloader"
generator_file="$batocera_repo/system/rgs/generators/linuxloader/linuxloaderGenerator.py"
build32="$repo_dir/build-bato"
build64="$repo_dir/build-bato64"
toolchain32="$batocera_output/x86_wow64/host/bin"
toolchain64="$batocera_output/x86_64/host/bin"

for path in \
    "$batocera_repo" \
    "$toolchain32/i686-buildroot-linux-gnu-gcc" \
    "$toolchain32/i686-buildroot-linux-gnu-g++" \
    "$toolchain64/x86_64-buildroot-linux-gnu-gcc" \
    "$toolchain64/x86_64-buildroot-linux-gnu-g++" \
    "$generator_file" \
    "$loader_dir/ghostbusters/libcsv.so" \
    "$loader_dir/ghostbusters/libesd.so.0"; do
    if [[ ! -e "$path" ]]; then
        printf 'Required path does not exist: %s\n' "$path" >&2
        exit 1
    fi
done

printf 'Configuring and building the portable i686 loader...\n'
cmake -S "$repo_dir" -B "$build32" \
    -DCMAKE_C_COMPILER="$toolchain32/i686-buildroot-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$toolchain32/i686-buildroot-linux-gnu-g++"
cmake --build "$build32" --parallel "${JOBS:-$(nproc)}" \
    --target linuxloader linuxloader_exe

printf 'Configuring and building the portable x86_64 loader...\n'
cmake -S "$repo_dir" -B "$build64" \
    -DCMAKE_C_COMPILER="$toolchain64/x86_64-buildroot-linux-gnu-gcc" \
    -DCMAKE_CXX_COMPILER="$toolchain64/x86_64-buildroot-linux-gnu-g++"
cmake --build "$build64" --parallel "${JOBS:-$(nproc)}" \
    --target linuxloader64 loader64

printf 'Updating local Batocera repo at %s...\n' "$batocera_repo"
install -m 755 "$build32/linuxloader" "$loader_dir/linuxloader"
install -m 755 "$build32/linuxloader.so" "$loader_dir/linuxloader.so"
install -m 755 "$build64/linuxloader64" "$loader_dir/linuxloader64"
install -m 755 "$build64/linuxloader64.so" "$loader_dir/linuxloader64.so"

printf 'Deploying loader to %s...\n' "$batocera_host"
tar -C "$loader_dir" -cf - \
    linuxloader linuxloader.so linuxloader64 linuxloader64.so ghostbusters \
    -C "$batocera_repo/system/rgs" generators/linuxloader/linuxloaderGenerator.py |
    ssh "$batocera_host" '
        set -eu
        deploy_dir=/userdata/system/rgs/emulators/linuxloader
        mkdir -p "$deploy_dir"
        staging_dir=$(mktemp -d "$deploy_dir/.deploy.XXXXXX")
        trap "rm -rf -- \"$staging_dir\"" EXIT
        tar -C "$staging_dir" -xf -
        # Rename complete files into place, keeping a running game on its
        # existing mapped library instead of truncating that library.
        for artifact in linuxloader linuxloader.so linuxloader64 linuxloader64.so; do
            mv -f -- "$staging_dir/$artifact" "$deploy_dir/$artifact"
        done
        mkdir -p "$deploy_dir/ghostbusters"
        for artifact in "$staging_dir"/ghostbusters/*; do
            mv -f -- "$artifact" "$deploy_dir/ghostbusters/"
        done
        generator_dir=/userdata/system/rgs/generators/linuxloader
        mkdir -p "$generator_dir"
        mv -f -- "$staging_dir/generators/linuxloader/linuxloaderGenerator.py" "$generator_dir/"
    '

printf 'Deployed loader binaries, generator and Ghostbusters libraries to %s.\n' "$batocera_host"
