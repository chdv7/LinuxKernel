#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/string.h>

#include "vmodem.h"

const char *vmodem_call_state_name(enum vmodem_call_state state)
{
	switch (state) {
	case VMODEM_CALL_IDLE:
		return "idle";
	case VMODEM_CALL_INCOMING:
		return "incoming";
	case VMODEM_CALL_ACTIVE:
		return "active";
	default:
		return "unknown";
	}
}

/*
 * Сброс изменяемой части состояния. state_lock уже должен быть захвачен.
 * IMEI намеренно не меняем: это идентификатор конкретного экземпляра модема.
 */
void vmodem_state_reset_locked(struct vmodem_device *vmodem)
{
	struct vmodem_state *state = &vmodem->state;

	state->echo_enabled = true;
	state->registered = true;
	state->signal_level = VMODEM_DEFAULT_SIGNAL;
	state->sim_ready = true;
	state->call_state = VMODEM_CALL_IDLE;
	state->call_incoming = false;
	strscpy(state->operator_name, "VMODEM", sizeof(state->operator_name));
	state->dial_number[0] = '\0';
}

void vmodem_state_reset(struct vmodem_device *vmodem)
{
	mutex_lock(&vmodem->state_lock);
	vmodem_state_reset_locked(vmodem);
	mutex_unlock(&vmodem->state_lock);
}

void vmodem_state_init(struct vmodem_device *vmodem)
{
	mutex_init(&vmodem->state_lock);
	memset(&vmodem->state, 0, sizeof(vmodem->state));

	/* Для каждого /dev/vmodemN формируем стабильный 15-значный IMEI. */
	snprintf(vmodem->state.imei, sizeof(vmodem->state.imei),
		 "35678901234%04u", vmodem->index);
	vmodem_state_reset_locked(vmodem);
}
