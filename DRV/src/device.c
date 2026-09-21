#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/printk.h>

#include "vmodem.h"

static int vmodem_open(struct inode *inode, struct file *file)
{
	unsigned int index = iminor(inode);

	if (index >= vmodem_count)
		return -ENODEV;

	file->private_data = &vmodems[index];
	return 0;
}

static int vmodem_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t vmodem_read(struct file *file, char __user *buffer, size_t count,
			   loff_t *offset)
{
	return 0;
}

static ssize_t vmodem_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *offset)
{
	return count;
}

static long vmodem_ioctl(struct file *file, unsigned int cmd,
			 unsigned long arg)
{
	return -ENOTTY;
}

static const struct file_operations vmodem_fops = {
	.owner = THIS_MODULE,
	.open = vmodem_open,
	.release = vmodem_release,
	.read = vmodem_read,
	.write = vmodem_write,
	.unlocked_ioctl = vmodem_ioctl,
	.llseek = noop_llseek,
};

int vmodem_devices_create(void)
{
	unsigned int i;
	int ret;

	ret = alloc_chrdev_region(&vmodem_devt, 0, vmodem_count, VMODEM_NAME);
	if (ret)
		return ret;

	cdev_init(&vmodem_cdev, &vmodem_fops);
	vmodem_cdev.owner = THIS_MODULE;

	ret = cdev_add(&vmodem_cdev, vmodem_devt, vmodem_count);
	if (ret)
		goto err_unregister;

	vmodem_class = class_create(VMODEM_NAME);
	if (IS_ERR(vmodem_class)) {
		ret = PTR_ERR(vmodem_class);
		vmodem_class = NULL;
		goto err_cdev;
	}

	for (i = 0; i < vmodem_count; ++i) {
		vmodems[i].index = i;
		vmodems[i].device = device_create(vmodem_class, NULL,
						  MKDEV(MAJOR(vmodem_devt), i),
						  &vmodems[i], "vmodem%u", i);
		if (IS_ERR(vmodems[i].device)) {
			ret = PTR_ERR(vmodems[i].device);
			vmodems[i].device = NULL;
			goto err_devices;
		}
	}

	return 0;

err_devices:
	while (i > 0) {
		--i;
		device_destroy(vmodem_class, MKDEV(MAJOR(vmodem_devt), i));
		vmodems[i].device = NULL;
	}
	class_destroy(vmodem_class);
	vmodem_class = NULL;
err_cdev:
	cdev_del(&vmodem_cdev);
err_unregister:
	unregister_chrdev_region(vmodem_devt, vmodem_count);
	return ret;
}

void vmodem_devices_destroy(void)
{
	unsigned int i;

	for (i = 0; i < vmodem_count; ++i) {
		if (vmodems[i].device) {
			device_destroy(vmodem_class,
				       MKDEV(MAJOR(vmodem_devt), i));
			vmodems[i].device = NULL;
		}
	}

	if (vmodem_class) {
		class_destroy(vmodem_class);
		vmodem_class = NULL;
	}

	cdev_del(&vmodem_cdev);
	unregister_chrdev_region(vmodem_devt, vmodem_count);
}
