#!/bin/sh
set -eu

MODULE=kernel_msgpool
PARAM_DIR=/sys/module/$MODULE/parameters
TEST_MARKER="kernel_msgpool_check_$$"

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

send_message() {
	printf '%s\n' "$1" | sudo tee "$PARAM_DIR/send" >/dev/null
}

cleanup
sudo insmod "$MODULE.ko" alloc_type=0 pool_min_nr=8 interval_ms=10000
trap cleanup EXIT

printf '%s\n' "$TEST_MARKER" | sudo tee /dev/kmsg >/dev/null

printf 'Initial parameters:\n'
printf 'alloc_type=' && cat "$PARAM_DIR/alloc_type"
printf 'pool_min_nr=' && cat "$PARAM_DIR/pool_min_nr"
printf 'interval_ms=' && cat "$PARAM_DIR/interval_ms"
printf 'inbox=' && cat "$PARAM_DIR/inbox"

printf '\n=== kmem_cache: queue overflow ===\n'
i=1
while [ "$i" -le 16 ]; do
	send_message "cache message $i"
	i=$((i + 1))
done

if send_message "overflow" 2>/dev/null; then
	printf 'ERROR: 17th message was accepted\n'
	exit 1
else
	printf '17th message rejected as expected\n'
fi

stats=$(cat "$PARAM_DIR/stats")
printf '%s\n' "$stats"
printf '%s\n' "$stats" | grep -q 'sent=16'
printf '%s\n' "$stats" | grep -q 'dropped=1'
printf '%s\n' "$stats" | grep -q 'queued=16'

printf '1\n' | sudo tee "$PARAM_DIR/flush" >/dev/null
stats=$(cat "$PARAM_DIR/stats")
printf '%s\n' "$stats"
printf '%s\n' "$stats" | grep -q 'flushed=16'
printf '%s\n' "$stats" | grep -q 'queued=0'

printf '\n=== mempool: timer consumer ===\n'
printf '1\n' | sudo tee "$PARAM_DIR/alloc_type" >/dev/null
printf '4\n' | sudo tee "$PARAM_DIR/pool_min_nr" >/dev/null
printf '500\n' | sudo tee "$PARAM_DIR/interval_ms" >/dev/null

send_message "mempool one"
send_message "mempool two"
sleep 2

inbox=$(cat "$PARAM_DIR/inbox")
stats=$(cat "$PARAM_DIR/stats")
printf 'inbox=%s\n' "$inbox"
printf '%s\n' "$stats"

if [ "$inbox" != "mempool two" ]; then
	printf 'ERROR: unexpected inbox value\n'
	exit 1
fi

printf '%s\n' "$stats" | grep -q 'alloc=mempool'
printf '%s\n' "$stats" | grep -q 'queued=0'

if printf '2\n' | sudo tee "$PARAM_DIR/alloc_type" >/dev/null 2>&1; then
	printf 'ERROR: invalid alloc_type was accepted\n'
	exit 1
fi

if printf '0\n' | sudo tee "$PARAM_DIR/pool_min_nr" >/dev/null 2>&1; then
	printf 'ERROR: invalid pool_min_nr was accepted\n'
	exit 1
fi

if printf '50\n' | sudo tee "$PARAM_DIR/interval_ms" >/dev/null 2>&1; then
	printf 'ERROR: invalid interval_ms was accepted\n'
	exit 1
fi

printf '\nMessages from this check:\n'
sudo dmesg | sed -n "/$TEST_MARKER/,\$p" | grep -E 'msgpool:|kernel_msgpool_check_' || true

printf '\nCheck completed successfully\n'
