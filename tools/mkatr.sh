#!/bin/sh
# mkatr.sh -- build a bootable Atari DOS 2.5 disk image.
#
# usage: mkatr.sh DIR2ATR DOS25_DIR SECTORS OUTPUT.atr ENTRY...
#
# Each ENTRY places one host file on the disk:
#   NAME=PATH   copy PATH verbatim as NAME (binaries)
#   NAME~PATH   copy PATH as NAME, converting LF to the ATASCII EOL ($9B);
#               use this for text files, since cc65's stdio only treats
#               $9B as a line terminator
#
# DOS.SYS and DUP.SYS are always taken from DOS25_DIR. SECTORS is 720
# (single density, 90K) or 1040 (enhanced density, 130K).
set -eu

if [ $# -lt 4 ]; then
    echo "usage: $0 DIR2ATR DOS25_DIR SECTORS OUTPUT.atr [NAME=PATH|NAME~PATH]..." >&2
    exit 2
fi

dir2atr=$1
dos25=$2
sectors=$3
out=$4
shift 4

stage="$out.d"
rm -rf "$stage" "$out"
mkdir -p "$stage"

cp "$dos25/DOS.SYS" "$stage/DOS.SYS"
cp "$dos25/DUP.SYS" "$stage/DUP.SYS"

for entry in "$@"; do
    case $entry in
    *=*)
        cp "${entry#*=}" "$stage/${entry%%=*}"
        ;;
    *~*)
        LC_ALL=C tr '\012' '\233' <"${entry#*~}" >"$stage/${entry%%~*}"
        ;;
    *)
        echo "error: bad entry '$entry' (expected NAME=PATH or NAME~PATH)" >&2
        exit 2
        ;;
    esac
done

# -b Dos25 writes real DOS 2.5 boot sectors that load DOS.SYS.
if ! log=$("$dir2atr" -b Dos25 "$sectors" "$out" "$stage" 2>&1); then
    echo "$log" >&2
    rm -rf "$stage" "$out"
    exit 1
fi
rm -rf "$stage"
