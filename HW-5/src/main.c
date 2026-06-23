#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/printk.h>

#include "kernel_alloc.h"

static int __init kernel_alloc_init(void)
{
	int ret;

	ret = allocator_init();
	if (ret != ALLOC_OK) {
		pr_err("failed to initialize allocator: %d\n", ret);
		return -ENOMEM;
	}

	pr_info("module loaded\n");

	return 0;
}

static void __exit kernel_alloc_exit(void)
{
	allocator_cleanup();
	pr_info("module unloaded\n");
}

module_init(kernel_alloc_init);
module_exit(kernel_alloc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("HW-5 bitmap memory allocator kernel module");
