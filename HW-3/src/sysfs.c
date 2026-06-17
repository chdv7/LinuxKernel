#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "kernel_stack.h"
#include "stack_ops.h"

struct kernel_stack_kobject {
	struct kobject kobj;
};

static struct kernel_stack_kobject *stack_kobject;

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

/*
 * DEVICE_ATTR_* создаёт struct device_attribute, но каталог задания должен
 * находиться под /sys/kernel, то есть принадлежать обычному kobject.
 * Поэтому kobject использует собственный sysfs_ops-адаптер, который вызывает
 * show/store из struct device_attribute. Параметр struct device здесь не нужен.
 */
static ssize_t stack_attr_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct device_attribute *dev_attr;

	(void)kobj;

	dev_attr = container_of(attr, struct device_attribute, attr);
	if (!dev_attr->show)
		return -EIO;

	return dev_attr->show(NULL, dev_attr, buf);
}

static ssize_t stack_attr_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buf, size_t count)
{
	struct device_attribute *dev_attr;

	(void)kobj;

	dev_attr = container_of(attr, struct device_attribute, attr);
	if (!dev_attr->store)
		return -EIO;

	return dev_attr->store(NULL, dev_attr, buf, count);
}

static const struct sysfs_ops stack_sysfs_ops = {
	.show = stack_attr_show,
	.store = stack_attr_store,
};

static void stack_kobject_release(struct kobject *kobj)
{
	struct kernel_stack_kobject *object;

	object = container_of(kobj, struct kernel_stack_kobject, kobj);
	kfree(object);
}

static const struct kobj_type stack_ktype = {
	.release = stack_kobject_release,
	.sysfs_ops = &stack_sysfs_ops,
};

int kernel_stack_sysfs_init(void)
{
	struct kernel_stack_kobject *object;
	int result;

	object = kzalloc(sizeof(*object), GFP_KERNEL);
	if (!object)
		return -ENOMEM;

	result = kobject_init_and_add(&object->kobj, &stack_ktype,
				      kernel_kobj, "kernel_stack");
	if (result) {
		kobject_put(&object->kobj);
		return result;
	}

	result = sysfs_create_group(&object->kobj, &stack_attr_group);
	if (result) {
		kobject_put(&object->kobj);
		return result;
	}

	stack_kobject = object;
	return 0;
}

void kernel_stack_sysfs_exit(void)
{
	if (!stack_kobject)
		return;

	sysfs_remove_group(&stack_kobject->kobj, &stack_attr_group);
	kobject_put(&stack_kobject->kobj);
	stack_kobject = NULL;
}
