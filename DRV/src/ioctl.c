#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "vmodem.h"

/*
 * Копирует внутреннее состояние в ABI-структуру для userspace.
 * Вызывается только при захваченном state_lock.
 */
static void fill_user_state(struct vmodem_device *vmodem,
			    struct vmodem_user_state *user_state)
{
	struct vmodem_state *state = &vmodem->state;

	memset(user_state, 0, sizeof(*user_state));
	user_state->index = vmodem->index;
	user_state->echo_enabled = state->echo_enabled;
	user_state->registered = state->registered;
	user_state->signal_level = state->signal_level;
	user_state->sim_ready = state->sim_ready;
	user_state->call_state = state->call_state;
	user_state->connected = state->connected;
	strscpy(user_state->operator_name, state->operator_name,
		sizeof(user_state->operator_name));
	strscpy(user_state->dial_number, state->dial_number,
		sizeof(user_state->dial_number));
	strscpy(user_state->imei, state->imei, sizeof(user_state->imei));
	strscpy(user_state->imsi, state->imsi, sizeof(user_state->imsi));
}

long vmodem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vmodem_device *vmodem = file->private_data;
	struct vmodem_user_state user_state;

	/* Чужие ioctl-команды к нашему драйверу не относятся. */
	if (_IOC_TYPE(cmd) != VMODEM_IOCTL_MAGIC)
		return -ENOTTY;

	switch (cmd) {
	case VMODEM_IOCTL_GET_STATE:
		/* Снимаем согласованный snapshot состояния модема. */
		mutex_lock(&vmodem->state_lock);
		fill_user_state(vmodem, &user_state);
		mutex_unlock(&vmodem->state_lock);

		if (copy_to_user((void __user *)arg, &user_state,
				 sizeof(user_state)))
			return -EFAULT;
		return 0;

	case VMODEM_IOCTL_RESET:
		/* Эквивалент программного сброса состояния модема. */
		vmodem_state_reset(vmodem);
		return 0;

	default:
		return -ENOTTY;
	}
}
