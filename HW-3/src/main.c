#include <linux/init.h>
#include <linux/module.h>

#include "kernel_stack.h"
#include "stack_ops.h"

static int __init kernel_stack_init_module(void)
{
	int result;

	stack_init();

	result = kernel_stack_sysfs_init();
	if (result) {
		stack_clear();
		pr_err("kernel_stack: failed to create sysfs interface: %d\n",
		       result);
		return result;
	}

	pr_info("kernel_stack: module loaded\n");
	pr_info("kernel_stack: sysfs path is /sys/kernel/kernel_stack/\n");
	return 0;
}

static void __exit kernel_stack_exit_module(void)
{
	kernel_stack_sysfs_exit();
	stack_clear();
	pr_info("kernel_stack: module unloaded\n");
}

module_init(kernel_stack_init_module);
module_exit(kernel_stack_exit_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("Linked-list stack controlled through sysfs");
MODULE_VERSION("1.0");
