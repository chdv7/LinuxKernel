#!/bin/sh
set -eu

MODULE=kernel_timer
PARAM_DIR=/sys/module/$MODULE/parameters
INTERVAL_MS=30000
EXPECTED_CALLBACKS=10
TEST_MARKER="kernel_timer_test_start_$$"

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

cleanup
trap cleanup EXIT

printf 'Loading module with 30 second interval...\n'
sudo insmod "$MODULE.ko" interval_ms="$INTERVAL_MS"

printf 'interval_ms='
cat "$PARAM_DIR/interval_ms"

# Записываем уникальный маркер начала теста непосредственно в kernel log.
printf '<6>%s\n' "$TEST_MARKER" | sudo tee /dev/kmsg >/dev/null

printf '\nTest started: %s\n' "$TEST_MARKER"
printf 'Waiting 5 minutes 5 seconds...\n\n'

sleep 305

# Берём только часть kernel log после маркера текущего запуска.
TEST_LOG=$(sudo dmesg | sed -n "/$TEST_MARKER/,\$p")

printf 'Timer messages:\n'
printf '%s\n' "$TEST_LOG" | grep "$MODULE: min=" || true

COUNT=$(printf '%s\n' "$TEST_LOG" |
	grep -c "$MODULE: min=" || true)

printf '\nCallbacks: %d\n' "$COUNT"

if [ "$COUNT" -ne "$EXPECTED_CALLBACKS" ]; then
	printf 'ERROR: expected exactly %d timer callbacks, got %d\n' \
		"$EXPECTED_CALLBACKS" "$COUNT"
	exit 1
fi

printf 'Exactly %d callbacks received\n' "$EXPECTED_CALLBACKS"

printf '\nWaiting another 35 seconds to verify that timer stopped...\n'
sleep 35

TEST_LOG=$(sudo dmesg | sed -n "/$TEST_MARKER/,\$p")

COUNT_AFTER=$(printf '%s\n' "$TEST_LOG" |
	grep -c "$MODULE: min=" || true)

if [ "$COUNT_AFTER" -ne "$EXPECTED_CALLBACKS" ]; then
	printf 'ERROR: timer continued after 5 minutes, callbacks=%d\n' \
		"$COUNT_AFTER"
	exit 1
fi

printf 'Timer stopped after 5 minutes as expected\n'
printf 'Long interval check completed successfully\n'

