#!/bin/sh
set -eu

MODULE=kernel_sync
PARAM_DIR=/sys/module/$MODULE/parameters

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

run_case() {
	lock_type="$1"
	lock_name="$2"

	printf '\n=== %s ===\n' "$lock_name"
	printf '%s\n' "$lock_type" | sudo tee "$PARAM_DIR/lock_type" >/dev/null
	printf '1\n' | sudo tee "$PARAM_DIR/reset" >/dev/null
	printf '1\n' | sudo tee "$PARAM_DIR/run" >/dev/null

	result=$(cat "$PARAM_DIR/result")
	stats=$(cat "$PARAM_DIR/stats")

	printf '%s\n' "$result"
	printf '%s\n' "$stats"
}

cleanup
sudo insmod "$MODULE.ko" num_threads=32 iterations=5000 lock_type=0
trap cleanup EXIT

printf 'Initial parameters:\n'
printf 'num_threads=' && cat "$PARAM_DIR/num_threads"
printf 'iterations=' && cat "$PARAM_DIR/iterations"
printf 'lock_type=' && cat "$PARAM_DIR/lock_type"

run_case 0 spinlock
run_case 1 mutex
run_case 2 semaphore

if printf '33\n' | sudo tee "$PARAM_DIR/num_threads" >/dev/null 2>&1; then
	printf 'ERROR: invalid num_threads was accepted\n'
	exit 1
else
	printf '\nInvalid num_threads rejected as expected\n'
fi

if printf '3\n' | sudo tee "$PARAM_DIR/lock_type" >/dev/null 2>&1; then
	printf 'ERROR: invalid lock_type was accepted\n'
	exit 1
else
	printf 'Invalid lock_type rejected as expected\n'
fi

printf '\nCheck completed successfully\n'
