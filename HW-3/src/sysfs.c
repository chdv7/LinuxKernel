#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "kernel_stack.h"
#include "stack_ops.h"


static struct kobject * stack_kobject;

static ssize_t push_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	int value;
	int result;

	(void)dev;
	(void)attr;

	result = kstrtoint(buf, 10, &value);
	if (result)
		return result;

	result = stack_push(value);
	if (result == STACK_NOMEM)
		return -ENOMEM;
	if (result == STACK_INVALID)
		return -EINVAL;
	if (result != STACK_OK)
		return -EIO;

	return count;
}

static ssize_t pop_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int value;
	int result;

	(void)dev;
	(void)attr;

	result = stack_pop_value(&value);
	if (result == STACK_EMPTY)
		return sysfs_emit(buf, "error: stack is empty\n");
	if (result != STACK_OK)
		return sysfs_emit(buf, "error: stack operation failed\n");

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t peek_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	int value;
	int result;

	(void)dev;
	(void)attr;

	result = stack_peek_value(&value);
	if (result == STACK_EMPTY)
		return sysfs_emit(buf, "error: stack is empty\n");
	if (result != STACK_OK)
		return sysfs_emit(buf, "error: stack operation failed\n");

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t size_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	(void)dev;
	(void)attr;

	return sysfs_emit(buf, "%d\n", stack_size());
}

static ssize_t is_empty_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	(void)dev;
	(void)attr;

	return sysfs_emit(buf, "%d\n", stack_is_empty());
}

static ssize_t clear_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	(void)dev;
	(void)attr;
	(void)buf;

	stack_clear();
	return count;
}

static DEVICE_ATTR_WO(push);
static DEVICE_ATTR_RO(pop);
static DEVICE_ATTR_RO(peek);
static DEVICE_ATTR_RO(size);
static DEVICE_ATTR_RO(is_empty);
static DEVICE_ATTR_WO(clear);

static struct attribute *stack_attrs[] = {
	&dev_attr_push.attr,
	&dev_attr_pop.attr,
	&dev_attr_peek.attr,
	&dev_attr_size.attr,
	&dev_attr_is_empty.attr,
	&dev_attr_clear.attr,
	NULL,
};

static const struct attribute_group stack_attr_group = {
	.attrs = stack_attrs,
};

int kernel_stack_sysfs_init(void){
	
    struct kobject *object = kobject_create_and_add("kernel_stack", kernel_kobj);
    if (!object) {
        return -ENOMEM;
	}

	int result = sysfs_create_group(object, &stack_attr_group);
	if (result) {
		kobject_put(object);
		return result;
	}

	stack_kobject = object;
	return 0;
}

void kernel_stack_sysfs_exit(void)
{
	if (!stack_kobject)
		return;

	sysfs_remove_group(stack_kobject, &stack_attr_group);
	kobject_put(stack_kobject);
	stack_kobject = NULL;
}
