#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>

#define FIFO_DEFAULT_SIZE 100
#define FIFO_OK 0
#define FIFO_NOMEM (-3)
#define FIFO_INVALID (-4)

int fifo_init(int size);
void fifo_destroy(void);

static int __init kernel_fifo_init(void)
{
	int result;

	result = fifo_init(FIFO_DEFAULT_SIZE);
	if (result != FIFO_OK) {
		pr_err("failed to initialize FIFO: %d\n", result);
		if (result == FIFO_NOMEM)
			return -ENOMEM;
		if (result == FIFO_INVALID)
			return -EINVAL;
		return -EIO;
	}

	pr_info("init\n");
	pr_info("capacity: %d elements\n", FIFO_DEFAULT_SIZE);
	return 0;
}

static void __exit kernel_fifo_exit(void)
{
	fifo_destroy();
	pr_info("exit\n");
}

module_init(kernel_fifo_init);
module_exit(kernel_fifo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("Integer FIFO based on Linux kfifo and module parameters");
MODULE_VERSION("1.0");
