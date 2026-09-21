#!/bin/sh
set -eu

MODULE=vmodem
MODEMS=3

cleanup() {
	sudo rmmod "$MODULE" 2>/dev/null || true
}

wait_for_device() {
	path="$1"
	attempt=0
	while [ ! -c "$path" ] && [ "$attempt" -lt 20 ]; do
		sleep 0.1
		attempt=$((attempt + 1))
	done
	[ -c "$path" ]
}

cleanup
trap cleanup EXIT

printf 'Loading %s with modems=%u\n' "$MODULE" "$MODEMS"
sudo insmod "$MODULE.ko" modems="$MODEMS"

printf '\n/proc/%s:\n' "$MODULE"
cat "/proc/$MODULE"

printf '\nChecking /dev and /sys...\n'
i=0
while [ "$i" -lt "$MODEMS" ]; do
	wait_for_device "/dev/vmodem$i"
	test -e "/sys/class/vmodem/vmodem$i"
	printf 'OK: /dev/vmodem%d and /sys/class/vmodem/vmodem%d\n' "$i" "$i"
	i=$((i + 1))
done

printf '\nChecking empty read/write...\n'
printf 'AT\r' | sudo tee /dev/vmodem0 >/dev/null
if [ -n "$(sudo dd if=/dev/vmodem0 bs=1 count=1 status=none)" ]; then
	printf 'ERROR: read must return EOF at step 1\n'
	exit 1
fi
printf 'OK: write accepted, read returned EOF\n'

printf '\nChecking invalid modem count...\n'
cleanup
if sudo insmod "$MODULE.ko" modems=17 2>/dev/null; then
	printf 'ERROR: modems=17 was accepted\n'
	cleanup
	exit 1
fi
printf 'OK: modems=17 rejected\n'

printf '\nStep 1 check completed successfully\n'
