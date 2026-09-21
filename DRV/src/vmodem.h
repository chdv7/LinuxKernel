#ifndef SRC_VMODEM_H_
#define SRC_VMODEM_H_

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/types.h>

#define VMODEM_NAME "vmodem"
#define VMODEM_MAX_DEVICES 16U
#define VMODEM_DEFAULT_DEVICES 1U

struct vmodem_device {
	unsigned int index;
	struct device *device;
};

extern unsigned int vmodem_count;
extern dev_t vmodem_devt;
extern struct cdev vmodem_cdev;
extern struct class *vmodem_class;
extern struct vmodem_device vmodems[VMODEM_MAX_DEVICES];

int vmodem_devices_create(void);
void vmodem_devices_destroy(void);

int vmodem_proc_create(void);
void vmodem_proc_destroy(void);

#endif /* SRC_VMODEM_H_ */
