#ifndef KERNEL_STACK_STACK_H_
#define KERNEL_STACK_STACK_H_

#include <linux/list.h>

struct stack {
	struct list_head elements;
	int size;
};

struct stack_entry {
	struct list_head list;
	int data;
};

extern struct stack kernel_stack_instance;

#endif  /* KERNEL_STACK_STACK_H_ */
