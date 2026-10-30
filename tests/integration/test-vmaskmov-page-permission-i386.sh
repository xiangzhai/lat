#!/bin/sh
set -eu

emulator=$(readlink -f "$1")
source_file=$(readlink -f "$2")
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

if [ "$(getconf PAGESIZE)" != 16384 ]; then
    echo "SKIP: VMASKMOV guest permission test requires a 16K host page"
    exit 77
fi

if ! command -v nasm >/dev/null 2>&1 || ! command -v ld.lld >/dev/null 2>&1; then
    echo "SKIP: nasm and ld.lld are required to build the i386 guest"
    exit 77
fi

guest="$workdir/vmaskmov-page-permission-i386"
for instruction in vmaskmovps vpmaskmovd vmaskmovpd vpmaskmovq; do
    case "$instruction" in
        vmaskmovpd|vpmaskmovq) quad=1 ;;
        *) quad=0 ;;
    esac
    for wide in 0 1; do
        nasm -DQUAD="$quad" -DMASKOP="$instruction" -DWIDE="$wide" \
            -f elf32 "$source_file" -o "$workdir/guest.o"
        ld.lld -m elf_i386 -static --build-id=none "$workdir/guest.o" -o "$guest"
        for mode in 1 2; do
            for case_name in rp rf wp wf rz wz rs ws; do
                if ! LATX_AOT=0 LATX_KZT=0 LATX_MT="$mode" \
                    timeout 30 "$emulator" "$guest" "$case_name"; then
                    echo "FAIL: $instruction wide=$wide MT=$mode $case_name" >&2
                    exit 1
                fi
            done
        done
    done
done

echo "PASS: VMASKMOV honors guest read/write permissions with LATX_MT=1/2"
