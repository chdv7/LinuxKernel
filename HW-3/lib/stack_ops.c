#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "stack.h"
#include "stack_ops.h"

static DEFINE_MUTEX(stack_mutex);

void stack_init(void)
{
	mutex_lock(&stack_mutex);
	INIT_LIST_HEAD(&kernel_stack_instance.elements);
	kernel_stack_instance.size = 0;
	mutex_unlock(&stack_mutex);
}

int stack_push(int value)
{
	struct stack_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return STACK_NOMEM;

	entry->data = value;

	mutex_lock(&stack_mutex);
	if (kernel_stack_instance.size == INT_MAX) {
		mutex_unlock(&stack_mutex);
		kfree(entry);
		return STACK_INVALID;
	}

	list_add(&entry->list, &kernel_stack_instance.elements);
	kernel_stack_instance.size++;
	mutex_unlock(&stack_mutex);

	return STACK_OK;
}

int stack_pop_value(int *value)
{
	struct stack_entry *entry;

	if (!value)
		return STACK_INVALID;

	mutex_lock(&stack_mutex);
	if (list_empty(&kernel_stack_instance.elements)) {
		mutex_unlock(&stack_mutex);
		return STACK_EMPTY;
	}

	entry = list_first_entry(&kernel_stack_instance.elements,
				 struct stack_entry, list);
	list_del(&entry->list);
	kernel_stack_instance.size--;
	*value = entry->data;
	mutex_unlock(&stack_mutex);

	kfree(entry);
	return STACK_OK;
}

int stack_pop(void)
{
	int value;
	int result;

	result = stack_pop_value(&value);
	if (result != STACK_OK)
		return result;

	return value;
}

int stack_peek_value(int *value)
{
	struct stack_entry *entry;

	if (!value)
		return STACK_INVALID;

	mutex_lock(&stack_mutex);
	if (list_empty(&kernel_stack_instance.elements)) {
		mutex_unlock(&stack_mutex);
		return STACK_EMPTY;
	}

	entry = list_first_entry(&kernel_stack_instance.elements,
				 struct stack_entry, list);
	*value = entry->data;
	mutex_unlock(&stack_mutex);

	return STACK_OK;
}

int stack_peek(void)
{
	int value;
	int result;

	result = stack_peek_value(&value);
	if (result != STACK_OK)
		return result;

	return value;
}

int stack_is_empty(void)
{
	int is_empty;

	mutex_lock(&stack_mutex);
	is_empty = list_empty(&kernel_stack_instance.elements) ? 1 : 0;
	mutex_unlock(&stack_mutex);

	return is_empty;
}

int stack_size(void)
{
	int size;

	mutex_lock(&stack_mutex);
	size = kernel_stack_instance.size;
	mutex_unlock(&stack_mutex);

	return size;
}

void stack_clear(void)
{
	struct stack_entry *entry;
	struct stack_entry *next;

	mutex_lock(&stack_mutex);
	list_for_each_entry_safe(entry, next,
				 &kernel_stack_instance.elements, list) {
		list_del(&entry->list);
		kfree(entry);
	}
	kernel_stack_instance.size = 0;
	mutex_unlock(&stack_mutex);
}
