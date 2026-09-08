#!/bin/sh
set -eu

MODULE=kernel_timer
PARAM_DIR=/sys/module/$MODULE/parameters

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

cleanup
sudo dmesg -C
sudo insmod "$MODULE.ko" interval_ms=1000
trap cleanup EXIT

printf 'interval_ms='
cat "$PARAM_DIR/interval_ms"

sleep 3

count=$(sudo dmesg | grep -c 'Hello, timer!' || true)
if [ "$count" -lt 2 ]; then
	printf 'ERROR: timer callback was not called enough times\n'
	exit 1
fi

printf '500\n' | sudo tee "$PARAM_DIR/interval_ms" >/dev/null
sleep 2

new_interval=$(cat "$PARAM_DIR/interval_ms")
if [ "$new_interval" != "500" ]; then
	printf 'ERROR: interval_ms was not updated\n'
	exit 1
fi

if printf '0\n' | sudo tee "$PARAM_DIR/interval_ms" >/dev/null 2>&1; then
	printf 'ERROR: invalid interval_ms was accepted\n'
	exit 1
fi

printf '\nTimer messages:\n'
sudo dmesg | grep 'Hello, timer!' || true
printf '\nCheck completed successfully\n'
