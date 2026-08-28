#!/bin/sh
set -eu

MODULE=kernel_pc
PARAM_DIR=/sys/module/$MODULE/parameters

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

run_case() {
	consumer_type="$1"
	consumer_name="$2"

	printf '\n=== %s ===\n' "$consumer_name"
	printf '%s\n' "$consumer_type" | sudo tee "$PARAM_DIR/consumer_type" >/dev/null
	printf '1\n' | sudo tee "$PARAM_DIR/reset" >/dev/null
	printf '1\n' | sudo tee "$PARAM_DIR/run" >/dev/null

	result=$(cat "$PARAM_DIR/result")
	stats=$(cat "$PARAM_DIR/stats")

	printf '%s\n' "$result"
	printf '%s\n' "$stats"

	case "$result" in
		*"consumer=$consumer_name"*" ok"*) ;;
		*)
			printf 'ERROR: unexpected result for %s\n' "$consumer_name"
			exit 1
			;;
	esac
}

cleanup
sudo insmod "$MODULE.ko" fifo_size=64 num_events=500 interval_us=500 consumer_type=0
trap cleanup EXIT

printf 'Initial parameters:\n'
printf 'fifo_size=' && cat "$PARAM_DIR/fifo_size"
printf 'num_events=' && cat "$PARAM_DIR/num_events"
printf 'interval_us=' && cat "$PARAM_DIR/interval_us"
printf 'consumer_type=' && cat "$PARAM_DIR/consumer_type"

run_case 0 tasklet
run_case 1 workqueue

if printf '2\n' | sudo tee "$PARAM_DIR/consumer_type" >/dev/null 2>&1; then
	printf 'ERROR: invalid consumer_type was accepted\n'
	exit 1
else
	printf '\nInvalid consumer_type rejected as expected\n'
fi

if printf '0\n' | sudo tee "$PARAM_DIR/run" >/dev/null 2>&1; then
	printf 'ERROR: invalid run value was accepted\n'
	exit 1
else
	printf 'Invalid run value rejected as expected\n'
fi

printf '\nCheck completed successfully\n'
