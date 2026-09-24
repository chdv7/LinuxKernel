#!/bin/sh
set -eu

MODULE=vmodem
MODEMS=3
DEV=/dev/vmodem0
SYS=/sys/class/vmodem/vmodem0

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

run_at() {
	command="$1"
	sudo sh -c '
		exec 3<>"$1"
		printf "%s\r" "$2" >&3
		timeout 0.2 cat <&3 || true
	' sh "$DEV" "$command"
}

cleanup
trap cleanup EXIT

printf 'Loading %s with modems=%u\n' "$MODULE" "$MODEMS"
sudo insmod "$MODULE.ko" modems="$MODEMS"

printf '\nChecking /dev and /sys...\n'
i=0
while [ "$i" -lt "$MODEMS" ]; do
	wait_for_device "/dev/vmodem$i"
	test -e "/sys/class/vmodem/vmodem$i/state"
	printf 'OK: vmodem%d\n' "$i"
	i=$((i + 1))
done

printf '\nAT test...\n'
response=$(run_at AT | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^AT$'
printf '%s\n' "$response" | grep -q '^OK$'

printf '\nPartial write test...\n'
response=$(sudo sh -c '
	exec 3<>"$1"
	printf A >&3
	printf T >&3
	printf "+CS" >&3
	printf "Q\r" >&3
	timeout 0.2 cat <&3 || true
' sh "$DEV" | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^+CSQ: 20,99$'

printf '\nImmediate AT-prefix echo test...\n'
response=$(sudo sh -c '
	exec 3>"$1"
	exec 4<"$1"

	printf "aaaqqaat" >&3
	timeout 0.2 dd bs=1 count=6 <&4 2>/dev/null || true

	printf "\r" >&3
	timeout 0.2 cat <&4 >/dev/null || true
' sh "$DEV")
[ "$response" = "aaaaat" ]
printf 'OK: A/a is echoed immediately, junk is hidden, T/t completes AT prefix\n'

printf '\nBackspace test...\n'
response=$(sudo sh -c '
	exec 3<>"$1"
	printf "ATX\b\r" >&3
	timeout 0.2 cat <&3 || true
' sh "$DEV" | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^OK$'
if printf '%s\n' "$response" | grep -q '^ERROR$'; then
	printf 'ERROR: backspace did not remove the previous character\n'
	exit 1
fi

printf '\nCase-insensitive / hidden-prefix-junk test...\n'
response=$(sudo sh -c '
	exec 3<>"$1"
	printf "garbageati\r" >&3
	timeout 0.2 cat <&3 || true
' sh "$DEV" | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^Virtual Modem 0$'

printf '\nEcho persistence test...\n'
run_at ATE0 >/dev/null
[ "$(cat "$SYS/echo")" = "0" ]
response=$(run_at AT | tr -d '\r')
printf '%s\n' "$response"
if printf '%s\n' "$response" | grep -q '^AT$'; then
	printf 'ERROR: AT prefix was echoed while echo is disabled\n'
	exit 1
fi
printf '%s\n' "$response" | grep -q '^OK$'
printf 'OK: with echo disabled, input is hidden completely, including AT\n'

printf '\nSysfs -> AT state test...\n'
echo 7 | sudo tee "$SYS/signal" >/dev/null
response=$(run_at 'AT+CSQ' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^+CSQ: 7,99$'

echo 0 | sudo tee "$SYS/registered" >/dev/null
response=$(run_at 'AT+CREG?' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^+CREG: 0,0$'

printf '\nIMSI test...\n'
response=$(run_at 'AT+CIMI' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^250011234560000$'
[ "$(cat "$SYS/imsi")" = "250011234560000" ]

echo 250010123456789 | sudo tee "$SYS/imsi" >/dev/null
response=$(run_at 'at+cimi' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^250010123456789$'

printf '\nReset test...\n'
run_at ATZ >/dev/null
[ "$(cat "$SYS/echo")" = "1" ]
[ "$(cat "$SYS/signal")" = "20" ]
[ "$(cat "$SYS/registered")" = "1" ]
[ "$(cat "$SYS/imsi")" = "250010123456789" ]

printf '\nATD / carrier-loss test...\n'
response=$(run_at 'ATD*99***1#' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^CONNECT$'
[ "$(cat "$SYS/connected")" = "1" ]

echo 0 | sudo tee "$SYS/connected" >/dev/null
response=$(sudo sh -c '
	exec 3<"$1"
	timeout 0.2 cat <&3 || true
' sh "$DEV" | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^NO CARRIER$'
[ "$(cat "$SYS/connected")" = "0" ]

printf '\n/proc/%s:\n' "$MODULE"
cat "/proc/$MODULE"

printf '\nStep 2 check completed successfully\n'
