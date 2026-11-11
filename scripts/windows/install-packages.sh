#!/usr/bin/env bash

echo "Install Packages Running..."

# Works both on a GitHub runner (non-root, sudo present) and inside a
# container running as root, where sudo may not be installed at all.
SUDO=""
if [[ $EUID -ne 0 ]]; then
    SUDO="sudo"
fi

$SUDO apt update
$SUDO apt -y install --no-install-recommends cmake mingw-w64 mingw-w64-i686-dev zip unzip make
