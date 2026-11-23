#!/bin/sh
# memreport.sh MAP CFG -- print how much RAM a linked image leaves free.
#
# The image loads at STARTADDRESS and may grow up to the C stack, which
# sits below __RESERVED_MEMORY__ (the screen) under MEMTOP $BC1F. ld65
# already refuses to link an image that does not fit; this just shows the
# margin so it is noticed before it runs out.
set -eu

map=$1
cfg=$2

hexval() {
    sed -n "s/.*$1: *type *= *weak, *value *= *\\\$\\([0-9A-Fa-f]*\\).*/\\1/p" "$cfg"
}

stack=$(hexval __STACKSIZE__)
reserved=$(hexval __RESERVED_MEMORY__)
end=$(awk '/^BSS +[0-9A-F]+ +[0-9A-F]+ +[0-9A-F]+/ { print $3 }' "$map")
if [ -z "$stack" ] || [ -z "$reserved" ] || [ -z "$end" ]; then
    echo "memreport: cannot read the memory map" >&2
    exit 1
fi
limit=$((0xBC20 - 0x$stack - 0x$reserved))
free=$((limit - 0x$end - 1))
printf '  %-8s %s ends at $%s, %d bytes free below the %d-byte stack\n' \
    MEM "$(basename "$map" .map).xex" "$end" "$free" $((0x$stack))
