#!/bin/sh
set -eu

: "${PS5_HOST:?set PS5_HOST to the FW 5.50 console address}"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <sample.elf> [remote-name]" >&2
    exit 2
fi

elf=$1
name=${2:-$(basename "$elf" .elf)}
websrv_timeout=${VULKAN_PS5_WEBSRV_TIMEOUT:-30}
case "$name" in
    *[!A-Za-z0-9_-]*|'')
        echo "remote-name must contain only letters, digits, _ or -" >&2
        exit 2
        ;;
esac
case "$websrv_timeout" in
    ''|*[!0-9]*|0)
        echo "VULKAN_PS5_WEBSRV_TIMEOUT must be a positive integer" >&2
        exit 2
        ;;
esac
if [ ! -f "$elf" ]; then
    echo "sample not found: $elf" >&2
    exit 2
fi

remote_dir="/data/homebrew/$name"
curl -sS --connect-timeout 3 --max-time 30 \
    "ftp://${PS5_HOST}:2121/" --quote "MKD $remote_dir" >/dev/null 2>&1 || true
curl -sS --connect-timeout 3 --max-time 30 \
    -T "$elf" "ftp://${PS5_HOST}:2121${remote_dir}/eboot.elf"
curl -sS --connect-timeout 3 --max-time "$websrv_timeout" \
    "http://${PS5_HOST}:8080/hbldr?pipe=1&daemon=0&path=${remote_dir}/eboot.elf"
