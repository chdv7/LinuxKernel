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

# Выполняет одну AT-команду через один и тот же open fd.
run_at() {
	command="$1"
	sudo sh -c '
		exec 3<>"$1"
		printf "%s\r" "$2" >&3
		# read() теперь блокирующий; timeout завершает чтение после ответа.
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


printf '\nShared stream / AT-prefix echo test...\n'
# До подтверждённого AT мусор не отображается. После T префикс AT
# появляется в общем output и может быть прочитан через другой fd.
response=$(sudo sh -c '
	exec 3>"$1"
	exec 4<"$1"

	printf "xxxA" >&3
	# Пока пришла только потенциальная A, читать нечего.
	timeout 0.1 dd bs=1 count=1 <&4 2>/dev/null || true

	printf T >&3
	# После подтверждения префикса AT другой fd должен увидеть именно "AT".
	timeout 0.2 dd bs=1 count=2 <&4 2>/dev/null || true

	# Обязательно завершаем начатую команду. Парсер принадлежит vmodemN,
	# а не открытому fd, поэтому незавершённый "AT" иначе перейдёт
	# в следующий тест. Ответ этой команды здесь только вычитываем.
	printf "\r" >&3
	timeout 0.2 cat <&4 >/dev/null || true
' sh "$DEV")
[ "$response" = "AT" ]
printf 'OK: junk is hidden, AT prefix is echoed through shared output\n'

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
	printf 'ERROR: echo is still enabled\n'
	exit 1
fi
printf '%s\n' "$response" | grep -q '^OK$'

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

# Проверяем, что IMSI можно изменить через sysfs и AT+CIMI видит новое значение.
echo 250010123456789 | sudo tee "$SYS/imsi" >/dev/null
response=$(run_at 'at+cimi' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^250010123456789$'

printf '\nReset test...\n'
run_at ATZ >/dev/null
[ "$(cat "$SYS/echo")" = "1" ]
[ "$(cat "$SYS/signal")" = "20" ]
[ "$(cat "$SYS/registered")" = "1" ]
# ATZ сбрасывает настройки модема, но не идентификатор SIM.
[ "$(cat "$SYS/imsi")" = "250010123456789" ]

printf '\nATD / carrier-loss test...\n'
response=$(run_at 'ATD*99***1#' | tr -d '\r')
printf '%s\n' "$response"
printf '%s\n' "$response" | grep -q '^CONNECT$'
[ "$(cat "$SYS/connected")" = "1" ]

# Внешняя потеря carrier должна породить unsolicited NO CARRIER.
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
