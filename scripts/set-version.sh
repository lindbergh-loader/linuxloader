#!/usr/bin/env bash
# Patch src/loader/version.h with the given X.Y.Z version string.
set -euo pipefail

VERSION="${1:-}"
VERSION_FILE="src/loader/version.h"

if [[ -z "$VERSION" ]]; then
  echo "Usage: $0 <major.minor.update>" >&2
  exit 1
fi

if [[ ! -f "$VERSION_FILE" ]]; then
  echo "Error: Version file '$VERSION_FILE' not found." >&2
  exit 1
fi

MAJOR=$(echo "$VERSION" | cut -d. -f1)
MINOR=$(echo "$VERSION" | cut -d. -f2)
UPDATE=$(echo "$VERSION" | cut -d. -f3)
[[ -z "$UPDATE" ]] && UPDATE=0

sed -i "s/\(#define MAJOR_VERSION \).*/\1$MAJOR/" "$VERSION_FILE"
sed -i "s/\(#define MINOR_VERSION \).*/\1$MINOR/" "$VERSION_FILE"
sed -i "s/\(#define UPDATE_VERSION \).*/\1$UPDATE/" "$VERSION_FILE"

echo "Updated $VERSION_FILE with version $MAJOR.$MINOR.$UPDATE"
