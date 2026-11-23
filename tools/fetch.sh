#!/bin/sh
# fetch.sh URL SHA256 OUTPUT
#
# Download URL to OUTPUT and verify its SHA-256. The file is only moved
# into place once the checksum matches, so a failed or tampered download
# never leaves a usable-looking artifact behind.
set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 URL SHA256 OUTPUT" >&2
    exit 2
fi

url=$1
want=$2
out=$3
tmp="$out.part"

if command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$1" | cut -d' ' -f1; }
else
    sha256() { shasum -a 256 "$1" | cut -d' ' -f1; }
fi

mkdir -p "$(dirname "$out")"
echo "  FETCH    $url"
curl -fsSL --retry 3 -o "$tmp" "$url"

got=$(sha256 "$tmp")
if [ "$got" != "$want" ]; then
    rm -f "$tmp"
    echo "error: checksum mismatch for $url" >&2
    echo "  expected $want" >&2
    echo "  got      $got" >&2
    exit 1
fi
mv "$tmp" "$out"
