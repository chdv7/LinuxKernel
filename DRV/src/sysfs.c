#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>

#include "vmodem.h"

/*
 * sysfs используется как управляющий интерфейс эмулятора. Пользователь может
 * читать состояние модема и менять параметры среды без AT-команд.
 */

/* device_create() сохранил struct vmodem_device в drvdata. */
static struct vmodem_device *to_vmodem(struct device *dev)
{
	return dev_get_drvdata(dev);
}

/* Копирует строку из sysfs store(), удаляя завершающий перевод строки. */
static int copy_sysfs_string(char *dst, size_t dst_size, const char *buf,
			     size_t count)
{
	char temp[VMODEM_COMMAND_MAX];
	char *value;

	if (!count || count >= sizeof(temp))
		return -EINVAL;

	memcpy(temp, buf, count);
	temp[count] = '\0';
	value = strim(temp);
	if (!value[0])
		return -EINVAL;

	if (strscpy(dst, value, dst_size) < 0)
		return -EINVAL;

	return 0;
}

/* echo: 0/1, соответствует ATE0/ATE1. */
static ssize_t echo_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;

	mutex_lock(&vmodem->state_lock);
	value = vmodem->state.echo_enabled;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%u\n", value ? 1U : 0U);
}

static ssize_t echo_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	vmodem->state.echo_enabled = value;
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(echo);

/* signal: 0..31 или 99, используется ответом AT+CSQ. */
static ssize_t signal_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	unsigned int value;

	mutex_lock(&vmodem->state_lock);
	value = vmodem->state.signal_level;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t signal_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;
	if (value > 31 && value != VMODEM_UNKNOWN_SIGNAL)
		return -EINVAL;

	mutex_lock(&vmodem->state_lock);
	vmodem->state.signal_level = value;
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(signal);

/* registered: имитирует регистрацию модема в сети. */
static ssize_t registered_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;

	mutex_lock(&vmodem->state_lock);
	value = vmodem->state.registered;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%u\n", value ? 1U : 0U);
}

static ssize_t registered_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	vmodem->state.registered = value;
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(registered);

/* sim_ready: состояние SIM, которое читает AT+CPIN?. */
static ssize_t sim_ready_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;

	mutex_lock(&vmodem->state_lock);
	value = vmodem->state.sim_ready;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%u\n", value ? 1U : 0U);
}

static ssize_t sim_ready_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	bool value;
	int ret;

	ret = kstrtobool(buf, &value);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	vmodem->state.sim_ready = value;
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(sim_ready);

/* operator: имя оператора для AT+COPS?. */
static ssize_t operator_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char operator_name[VMODEM_OPERATOR_MAX];

	mutex_lock(&vmodem->state_lock);
	strscpy(operator_name, vmodem->state.operator_name,
		sizeof(operator_name));
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%s\n", operator_name);
}

static ssize_t operator_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char value[VMODEM_OPERATOR_MAX];
	int ret;

	ret = copy_sysfs_string(value, sizeof(value), buf, count);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	strscpy(vmodem->state.operator_name, value,
		sizeof(vmodem->state.operator_name));
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(operator);

/* call_state: idle/incoming/active, можно менять для имитации звонка. */
static ssize_t call_state_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	enum vmodem_call_state state;

	mutex_lock(&vmodem->state_lock);
	state = vmodem->state.call_state;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%s\n", vmodem_call_state_name(state));
}

static ssize_t call_state_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char value[16];
	int ret;

	ret = copy_sysfs_string(value, sizeof(value), buf, count);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	if (!strcmp(value, "idle")) {
		vmodem->state.call_state = VMODEM_CALL_IDLE;
		vmodem->state.call_incoming = false;
		vmodem->state.dial_number[0] = '\0';
	} else if (!strcmp(value, "incoming")) {
		vmodem->state.call_state = VMODEM_CALL_INCOMING;
		vmodem->state.call_incoming = true;
	} else if (!strcmp(value, "active")) {
		vmodem->state.call_state = VMODEM_CALL_ACTIVE;
	} else {
		mutex_unlock(&vmodem->state_lock);
		return -EINVAL;
	}
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(call_state);

/* dial_number: номер текущего исходящего/входящего звонка. */
static ssize_t dial_number_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char number[VMODEM_NUMBER_MAX];

	mutex_lock(&vmodem->state_lock);
	strscpy(number, vmodem->state.dial_number, sizeof(number));
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%s\n", number);
}

static ssize_t dial_number_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char value[VMODEM_NUMBER_MAX];
	int ret;

	ret = copy_sysfs_string(value, sizeof(value), buf, count);
	if (ret)
		return ret;

	mutex_lock(&vmodem->state_lock);
	strscpy(vmodem->state.dial_number, value,
		sizeof(vmodem->state.dial_number));
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(dial_number);

/* imei: 15 цифр; доступен также через AT+CGSN. */
static ssize_t imei_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char imei[VMODEM_IMEI_MAX];

	mutex_lock(&vmodem->state_lock);
	strscpy(imei, vmodem->state.imei, sizeof(imei));
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(buf, "%s\n", imei);
}

static ssize_t imei_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	char value[VMODEM_IMEI_MAX];
	size_t i;
	int ret;

	ret = copy_sysfs_string(value, sizeof(value), buf, count);
	if (ret)
		return ret;
	if (strlen(value) != VMODEM_IMEI_MAX - 1)
		return -EINVAL;
	for (i = 0; i < VMODEM_IMEI_MAX - 1; ++i) {
		if (value[i] < '0' || value[i] > '9')
			return -EINVAL;
	}

	mutex_lock(&vmodem->state_lock);
	strscpy(vmodem->state.imei, value, sizeof(vmodem->state.imei));
	mutex_unlock(&vmodem->state_lock);
	return count;
}
static DEVICE_ATTR_RW(imei);

/* state: компактный read-only snapshot всех основных полей. */
static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct vmodem_device *vmodem = to_vmodem(dev);
	struct vmodem_state state;

	mutex_lock(&vmodem->state_lock);
	state = vmodem->state;
	mutex_unlock(&vmodem->state_lock);

	return sysfs_emit(
		buf,
		"echo=%u registered=%u signal=%u operator=%s sim_ready=%u call_state=%s dial_number=%s imei=%s\n",
		state.echo_enabled ? 1U : 0U, state.registered ? 1U : 0U,
		state.signal_level, state.operator_name, state.sim_ready ? 1U : 0U,
		vmodem_call_state_name(state.call_state), state.dial_number,
		state.imei);
}
static DEVICE_ATTR_RO(state);

/* Все атрибуты создаются одной группой для каждого /sys/class/vmodem/vmodemN. */
static struct attribute *vmodem_attrs[] = {
	&dev_attr_echo.attr,
	&dev_attr_signal.attr,
	&dev_attr_registered.attr,
	&dev_attr_sim_ready.attr,
	&dev_attr_operator.attr,
	&dev_attr_call_state.attr,
	&dev_attr_dial_number.attr,
	&dev_attr_imei.attr,
	&dev_attr_state.attr,
	NULL,
};

static const struct attribute_group vmodem_attr_group = {
	.attrs = vmodem_attrs,
};

int vmodem_sysfs_create(struct vmodem_device *vmodem)
{
	return sysfs_create_group(&vmodem->device->kobj, &vmodem_attr_group);
}

void vmodem_sysfs_destroy(struct vmodem_device *vmodem)
{
	if (vmodem->device)
		sysfs_remove_group(&vmodem->device->kobj, &vmodem_attr_group);
}
