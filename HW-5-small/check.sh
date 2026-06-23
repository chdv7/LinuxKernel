#!/bin/sh
set -eu

MODULE=kernel_alloc
PARAM_DIR=/sys/module/$MODULE/parameters

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

alloc_bytes() {
	bytes="$1"
	printf '%s\n' "$bytes" | sudo tee "$PARAM_DIR/alloc" >/dev/null
	cat "$PARAM_DIR/alloc" | awk -F': ' '/Last allocation/ { print $2 }'
}

free_addr() {
	addr="$1"
	printf '%s\n' "$addr" | sudo tee "$PARAM_DIR/free" >/dev/null
}

cleanup
sudo insmod "$MODULE.ko"
trap cleanup EXIT

printf 'Initial stats:\n'
cat "$PARAM_DIR/stats"

addr1=$(alloc_bytes 8192)
printf 'Allocated 8192 bytes at %s\n' "$addr1"

addr2=$(alloc_bytes 16384)
printf 'Allocated 16384 bytes at %s\n' "$addr2"

addr3=$(alloc_bytes 4096)
printf 'Allocated 4096 bytes at %s\n' "$addr3"

addr4=$(alloc_bytes 10000000)
printf 'Allocated 10000000 bytes at %s\n' "$addr4"

printf 'Stats after allocations:\n'
cat "$PARAM_DIR/stats"

printf 'Bitmap prefix:\n'
cat "$PARAM_DIR/bitmap_info" | cut -c 1-80

free_addr "$addr2"
printf 'Freed second allocation: %s\n' "$addr2"

printf 'Stats after allocations:\n'
cat "$PARAM_DIR/stats"

printf 'Bitmap prefix:\n'
cat "$PARAM_DIR/bitmap_info" | cut -c 1-80

free_addr "$addr1"
printf 'Freed first allocation: %s\n' "$addr1"

printf 'Stats after partial free:\n'
cat "$PARAM_DIR/stats"

free_addr "$addr3"
printf 'Freed third allocation: %s\n' "$addr3"

free_addr "$addr4"
printf 'Freed third allocation: %s\n' "$addr4"


printf 'Stats after full free:\n'
cat "$PARAM_DIR/stats"

if printf '0\n' | sudo tee "$PARAM_DIR/alloc" >/dev/null 2>&1; then
	printf 'ERROR: zero-size allocation was accepted\n'
	exit 1
else
	printf 'Zero-size allocation rejected as expected\n'
fi

if printf '0x1\n' | sudo tee "$PARAM_DIR/free" >/dev/null 2>&1; then
	printf 'ERROR: invalid free was accepted\n'
	exit 1
else
	printf 'Invalid free rejected as expected\n'
fi

printf 'Check completed successfully\n'
