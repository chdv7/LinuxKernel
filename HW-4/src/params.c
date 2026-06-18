#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>

#define FIFO_OK 0
#define FIFO_EMPTY (-1)
#define FIFO_FULL (-2)
#define FIFO_NOMEM (-3)
#define FIFO_INVALID (-4)

int fifo_enqueue(int value);
int fifo_dequeue_value(int *value);
int fifo_peek_value(int *value);
int fifo_is_empty(void);
int fifo_is_full(void);
int fifo_size(void);
int fifo_available(void);
void fifo_clear(void);

static int fifo_result_to_errno(int result)
{
	switch (result) {
	case FIFO_OK:
		return 0;
	case FIFO_FULL:
		return -ENOSPC;
	case FIFO_NOMEM:
		return -ENOMEM;
	case FIFO_INVALID:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static int param_set_enqueue(const char *value,
			     const struct kernel_param *)
{
	int number;
	int result;

	result = kstrtoint(value, 10, &number);
	if (result)
		return result;

	return fifo_result_to_errno(fifo_enqueue(number));
}

static int param_get_dequeue(char *buffer,
			     const struct kernel_param *)
{
	int result;
	int value;

	result = fifo_dequeue_value(&value);
	if (result == FIFO_EMPTY)
		return scnprintf(buffer, PAGE_SIZE, "error: FIFO is empty\n");
	if (result != FIFO_OK)
		return scnprintf(buffer, PAGE_SIZE,
				 "error: FIFO operation failed (%d)\n", result);

	return scnprintf(buffer, PAGE_SIZE, "%d\n", value);
}

static int param_get_peek(char *buffer,
			  const struct kernel_param *)
{
	int result;
	int value;

	result = fifo_peek_value(&value);
	if (result == FIFO_EMPTY)
		return scnprintf(buffer, PAGE_SIZE, "error: FIFO is empty\n");
	if (result != FIFO_OK)
		return scnprintf(buffer, PAGE_SIZE,
				 "error: FIFO operation failed (%d)\n", result);

	return scnprintf(buffer, PAGE_SIZE, "%d\n", value);
}

static int param_get_size(char *buffer,
			  const struct kernel_param *)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n", fifo_size());
}

static int param_get_available(char *buffer,
			       const struct kernel_param *)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n", fifo_available());
}

static int param_get_is_empty(char *buffer,
			      const struct kernel_param *)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n", fifo_is_empty());
}

static int param_get_is_full(char *buffer,
			     const struct kernel_param *)
{
	return scnprintf(buffer, PAGE_SIZE, "%d\n", fifo_is_full());
}

static int param_set_clear(const char *value,
			   const struct kernel_param *)
{
	fifo_clear();
	return 0;
}

static const struct kernel_param_ops enqueue_ops = {
	.set = param_set_enqueue,
};

static const struct kernel_param_ops dequeue_ops = {
	.get = param_get_dequeue,
};

static const struct kernel_param_ops peek_ops = {
	.get = param_get_peek,
};

static const struct kernel_param_ops size_ops = {
	.get = param_get_size,
};

static const struct kernel_param_ops available_ops = {
	.get = param_get_available,
};

static const struct kernel_param_ops is_empty_ops = {
	.get = param_get_is_empty,
};

static const struct kernel_param_ops is_full_ops = {
	.get = param_get_is_full,
};

static const struct kernel_param_ops clear_ops = {
	.set = param_set_clear,
};

module_param_cb(enqueue, &enqueue_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(enqueue, "Add an integer to the FIFO");

module_param_cb(dequeue, &dequeue_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(dequeue, "Read and remove the first FIFO element");

module_param_cb(peek, &peek_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(peek, "Read the first FIFO element without removing it");

module_param_cb(size, &size_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(size, "Current number of FIFO elements");

module_param_cb(available, &available_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(available, "Number of free FIFO slots");

module_param_cb(is_empty, &is_empty_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(is_empty, "Whether the FIFO is empty");

module_param_cb(is_full, &is_full_ops, NULL, S_IRUSR);
MODULE_PARM_DESC(is_full, "Whether the FIFO is full");

module_param_cb(clear, &clear_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(clear, "Clear the FIFO");
