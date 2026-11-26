#!/usr/bin/env bash
# Batocera build: the 32-bit loader, its libraries and the 64-bit loader,
# the targets of the local `make bato`, in linuxloader-batocera.tar.gz.
# Built with the host gcc; the host glibc must not be newer than
# Batocera's (2.40 for v43), which the check below enforces.
set -euo pipefail

build=build-batocera
stage=linuxloader-batocera
max_glibc=${MAX_GLIBC:-2.40}

cmake -S . -B "$build"
cmake --build "$build" --parallel "$(nproc)" --target \
    linuxloader_exe linuxloader linuxloader_freebsd \
    segaapi kswapapi posixtime glew_gvr arcaderegistry \
    linuxloader64 loader64

rm -rf "$stage"
mkdir -p "$stage"
cp -a "$build"/linuxloader "$build"/linuxloader.so "$build"/linuxloader_freebsd \
    "$build"/linuxloader64 "$build"/linuxloader64.so "$build"/g7_rt.so "$build"/halo_rt.so \
    "$build"/libsegaapi.so "$build"/libkswapapi.so \
    "$build"/libposixtime.so "$build"/libposixtime.so.1 "$build"/libposixtime.so.2.4 \
    "$build"/libGLEW.so "$build"/libGLEW.so.1.3 \
    "$build"/libarcaderegistry.so "$build"/libarcaderegistry.so.1 \
    "$stage"/

status=0
for f in "$stage"/*; do
    [[ -L $f ]] && continue
    need=$(objdump -T "$f" | { grep -o 'GLIBC_[0-9.]*' || true; } | sed 's/GLIBC_//' | sort -uV | tail -1)
    printf '%-40s GLIBC_%s\n' "${f#"$stage"/}" "$need"
    if [[ -n $need && $(printf '%s\n%s\n' "$need" "$max_glibc" | sort -V | tail -1) != "$max_glibc" ]]; then
        printf '%s needs GLIBC_%s, newer than %s\n' "$f" "$need" "$max_glibc" >&2
        status=1
    fi
done
[[ $status -eq 0 ]] || exit 1

tar -czf linuxloader-batocera.tar.gz -C "$stage" .
