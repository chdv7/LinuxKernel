#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>

#include "vmodem.h"

/* Количество виртуальных модемов задаётся при insmod: modems=1..16. */
unsigned int vmodem_count = VMODEM_DEFAULT_DEVICES;
dev_t vmodem_devt;
struct cdev vmodem_cdev;
struct class *vmodem_class;
struct vmodem_device vmodems[VMODEM_MAX_DEVICES];

module_param_named(modems, vmodem_count, uint, 0444);
MODULE_PARM_DESC(modems, "Number of virtual modems, 1..16");

static bool vmodem_count_valid(unsigned int count)
{
	return count >= 1 && count <= VMODEM_MAX_DEVICES;
}

static int __init vmodem_init(void)
{
	int ret;

	/* Не создаём частично валидный набор устройств. */
	if (!vmodem_count_valid(vmodem_count)) {
		pr_err("invalid modems=%u, expected 1..%u\n", vmodem_count,
		       VMODEM_MAX_DEVICES);
		return -EINVAL;
	}

	/* Регистрируем cdev, /dev/vmodemN и sysfs-атрибуты каждого модема. */
	ret = vmodem_devices_create();
	if (ret) {
		pr_err("failed to create devices: %d\n", ret);
		return ret;
	}

	/* /proc/vmodem показывает сводное состояние всех экземпляров. */
	ret = vmodem_proc_create();
	if (ret) {
		pr_err("failed to create /proc/%s: %d\n", VMODEM_NAME, ret);
		vmodem_devices_destroy();
		return ret;
	}

	pr_info("loaded: modems=%u major=%u\n", vmodem_count,
		MAJOR(vmodem_devt));
	return 0;
}

static void __exit vmodem_exit(void)
{
	vmodem_proc_destroy();
	vmodem_devices_destroy();
	pr_info("unloaded\n");
}

module_init(vmodem_init);
module_exit(vmodem_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("DRV virtual modem with AT commands and state");
