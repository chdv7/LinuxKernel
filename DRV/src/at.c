#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "vmodem.h"

/*
 * Безопасно дописывает фрагмент в ответ. vscnprintf() никогда не пишет
 * больше оставшегося размера буфера и возвращает фактически записанную длину.
 */
static size_t append_response(char *buffer, size_t size, size_t pos,
			      const char *format, ...)
{
	va_list args;
	int written;

	if (pos >= size)
		return pos;

	va_start(args, format);
	written = vscnprintf(buffer + pos, size - pos, format, args);
	va_end(args);

	return pos + written;
}

/* Разрешаем цифры и стандартные символы, встречающиеся в телефонном номере. */
static bool dial_number_valid(const char *number)
{
	const char *p;

	if (!number[0])
		return false;

	for (p = number; *p; ++p) {
		if (isdigit(*p) || *p == '+' || *p == '*' || *p == '#')
			continue;
		return false;
	}

	return true;
}

/* Обработка ATD<number>[;]. state_lock захвачен вызывающей функцией. */
static size_t handle_dial(struct vmodem_device *vmodem, const char *command,
			  char *response, size_t size, size_t pos)
{
	struct vmodem_state *state = &vmodem->state;
	const char *source = command + 3;
	char number[VMODEM_NUMBER_MAX];
	size_t source_len;
	size_t len;

	/*
	 * Не допускаем тихого усечения слишком длинного номера: такая команда
	 * должна завершиться ERROR, а не звонком на другой номер.
	 */
	source_len = strlen(source);
	if (source_len >= sizeof(number))
		return append_response(response, size, pos, "ERROR\r\n");

	strscpy(number, source, sizeof(number));
	len = strlen(number);
	if (len > 0 && number[len - 1] == ';')
		number[len - 1] = '\0';

	/* Звонок возможен только при готовой SIM, регистрации и отсутствии звонка. */
	if (!dial_number_valid(number) || !state->sim_ready ||
	    !state->registered || state->call_state != VMODEM_CALL_IDLE)
		return append_response(response, size, pos, "ERROR\r\n");

	strscpy(state->dial_number, number, sizeof(state->dial_number));
	state->call_state = VMODEM_CALL_ACTIVE;
	state->call_incoming = false;
	state->connected = true;

	/* После успешного дозвона модем переходит в online/data state. */
	return append_response(response, size, pos, "CONNECT\r\n");
}

/*
 * Декодирует одну уже собранную AT-команду и формирует ответ модема.
 * Все изменения состояния выполняются под state_lock.
 */
size_t vmodem_process_command(struct vmodem_device *vmodem, const char *command,
			      char *response, size_t response_size)
{
	struct vmodem_state *state = &vmodem->state;
	size_t pos = 0;

	if (!response_size)
		return 0;

	response[0] = '\0';

	mutex_lock(&vmodem->state_lock);

	/*
	 * Echo здесь не формируется. Символы отражаются немедленно в write(),
	 * по мере их поступления, как у настоящего AT-модема.
	 */
	if (!strcasecmp(command, "AT")) {
		pos = append_response(response, response_size, pos, "OK\r\n");
	} else if (!strcasecmp(command, "ATE0")) {
		state->echo_enabled = false;
		pos = append_response(response, response_size, pos, "OK\r\n");
	} else if (!strcasecmp(command, "ATE1")) {
		state->echo_enabled = true;
		pos = append_response(response, response_size, pos, "OK\r\n");
	} else if (!strcasecmp(command, "ATI")) {
		pos = append_response(response, response_size, pos,
				      "Virtual Modem %u\r\nOK\r\n",
				      vmodem->index);
	} else if (!strcasecmp(command, "ATZ") ||
		   !strcasecmp(command, "AT&F")) {
		vmodem_state_reset_locked(vmodem);
		pos = append_response(response, response_size, pos, "OK\r\n");
	} else if (!strcasecmp(command, "AT+CSQ")) {
		pos = append_response(response, response_size, pos,
				      "+CSQ: %u,99\r\nOK\r\n",
				      state->signal_level);
	} else if (!strcasecmp(command, "AT+CREG?")) {
		pos = append_response(response, response_size, pos,
				      "+CREG: 0,%u\r\nOK\r\n",
				      state->registered ? 1U : 0U);
	} else if (!strcasecmp(command, "AT+COPS?")) {
		if (state->registered)
			pos = append_response(response, response_size, pos,
					      "+COPS: 0,0,\"%s\"\r\nOK\r\n",
					      state->operator_name);
		else
			pos = append_response(response, response_size, pos,
					      "+COPS: 0\r\nOK\r\n");
	} else if (!strcasecmp(command, "AT+CPIN?")) {
		pos = append_response(response, response_size, pos,
				      "+CPIN: %s\r\nOK\r\n",
				      state->sim_ready ? "READY" : "SIM PIN");
	} else if (!strcasecmp(command, "AT+CGSN")) {
		pos = append_response(response, response_size, pos,
				      "%s\r\nOK\r\n", state->imei);
	} else if (!strcasecmp(command, "AT+CIMI")) {
		/* International Mobile Subscriber Identity текущей SIM. */
		pos = append_response(response, response_size, pos,
				      "%s\r\nOK\r\n", state->imsi);
	} else if (!strncasecmp(command, "ATD", 3)) {
		pos = handle_dial(vmodem, command, response, response_size, pos);
	} else if (!strcasecmp(command, "ATA")) {
		if (state->call_state != VMODEM_CALL_INCOMING) {
			pos = append_response(response, response_size, pos,
					      "ERROR\r\n");
		} else {
			state->call_state = VMODEM_CALL_ACTIVE;
			state->call_incoming = true;
			state->connected = true;
			pos = append_response(response, response_size, pos,
					      "OK\r\n");
		}
	} else if (!strcasecmp(command, "ATH")) {
		state->call_state = VMODEM_CALL_IDLE;
		state->call_incoming = false;
		state->connected = false;
		state->dial_number[0] = '\0';
		pos = append_response(response, response_size, pos, "OK\r\n");
	} else if (!strcasecmp(command, "AT+CLCC")) {
		/* Формат приближен к стандартному ответу +CLCC. */
		if (state->call_state == VMODEM_CALL_INCOMING) {
			pos = append_response(
				response, response_size, pos,
				"+CLCC: 1,1,4,0,0,\"%s\",129\r\nOK\r\n",
				state->dial_number);
		} else if (state->call_state == VMODEM_CALL_ACTIVE) {
			pos = append_response(
				response, response_size, pos,
				"+CLCC: 1,%u,0,0,0,\"%s\",129\r\nOK\r\n",
				state->call_incoming ? 1U : 0U,
				state->dial_number);
		} else {
			pos = append_response(response, response_size, pos,
					      "OK\r\n");
		}
	} else {
		/* Любая неподдерживаемая AT-команда завершается ERROR. */
		pos = append_response(response, response_size, pos, "ERROR\r\n");
	}

	mutex_unlock(&vmodem->state_lock);
	return pos;
}
