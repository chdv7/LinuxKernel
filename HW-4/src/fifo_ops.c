#include <linux/kfifo.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#define FIFO_OK 0
#define FIFO_EMPTY (-1)
#define FIFO_FULL (-2)
#define FIFO_NOMEM (-3)
#define FIFO_INVALID (-4)

struct fifo_device {
	struct kfifo queue;
	int max_size;
};

struct fifo_entry {
	int data;
};

int fifo_init(int size);
void fifo_destroy(void);
int fifo_enqueue(int value);
int fifo_dequeue_value(int *value);
int fifo_dequeue(void);
int fifo_peek_value(int *value);
int fifo_peek(void);
int fifo_is_empty(void);
int fifo_is_full(void);
int fifo_size(void);
int fifo_available(void);
void fifo_clear(void);

static struct fifo_device fifo_device;
static DEFINE_MUTEX(fifo_mutex);
static bool fifo_initialized;

static int fifo_size_locked(void)
{
	return kfifo_len(&fifo_device.queue) / sizeof(struct fifo_entry);
}

int fifo_init(int size)
{
	unsigned int buffer_size;
	int result;

	if (size <= 0 || size > UINT_MAX / sizeof(struct fifo_entry))
		return FIFO_INVALID;

	buffer_size = size * sizeof(struct fifo_entry);

	mutex_lock(&fifo_mutex);
	if (fifo_initialized) {
		mutex_unlock(&fifo_mutex);
		return FIFO_INVALID;
	}

	result = kfifo_alloc(&fifo_device.queue, buffer_size, GFP_KERNEL);
	if (result) {
		mutex_unlock(&fifo_mutex);
		return FIFO_NOMEM;
	}

	fifo_device.max_size = size;
	fifo_initialized = true;
	mutex_unlock(&fifo_mutex);

	return FIFO_OK;
}

void fifo_destroy(void)
{
	mutex_lock(&fifo_mutex);
	if (fifo_initialized) {
		kfifo_free(&fifo_device.queue);
		fifo_device.max_size = 0;
		fifo_initialized = false;
	}
	mutex_unlock(&fifo_mutex);
}

int fifo_enqueue(int value)
{
	struct fifo_entry entry = {
		.data = value,
	};
	unsigned int inserted;

	mutex_lock(&fifo_mutex);
	if (!fifo_initialized) {
		mutex_unlock(&fifo_mutex);
		return FIFO_INVALID;
	}

	if (fifo_size_locked() >= fifo_device.max_size) {
		mutex_unlock(&fifo_mutex);
		return FIFO_FULL;
	}

	inserted = kfifo_in(&fifo_device.queue, &entry, sizeof(entry));
	mutex_unlock(&fifo_mutex);

	return inserted == sizeof(entry) ? FIFO_OK : FIFO_FULL;
}

int fifo_dequeue_value(int *value)
{
	struct fifo_entry entry;
	unsigned int removed;

	if (!value)
		return FIFO_INVALID;

	mutex_lock(&fifo_mutex);
	if (!fifo_initialized) {
		mutex_unlock(&fifo_mutex);
		return FIFO_INVALID;
	}

	if (kfifo_is_empty(&fifo_device.queue)) {
		mutex_unlock(&fifo_mutex);
		return FIFO_EMPTY;
	}

	removed = kfifo_out(&fifo_device.queue, &entry, sizeof(entry));
	mutex_unlock(&fifo_mutex);

	if (removed != sizeof(entry))
		return FIFO_INVALID;

	*value = entry.data;
	return FIFO_OK;
}

int fifo_dequeue(void)
{
	int result;
	int value;

	result = fifo_dequeue_value(&value);
	if (result != FIFO_OK)
		return result;

	return value;
}

int fifo_peek_value(int *value)
{
	struct fifo_entry entry;
	unsigned int copied;

	if (!value)
		return FIFO_INVALID;

	mutex_lock(&fifo_mutex);
	if (!fifo_initialized) {
		mutex_unlock(&fifo_mutex);
		return FIFO_INVALID;
	}

	if (kfifo_is_empty(&fifo_device.queue)) {
		mutex_unlock(&fifo_mutex);
		return FIFO_EMPTY;
	}

	copied = kfifo_out_peek(&fifo_device.queue, &entry, sizeof(entry));
	mutex_unlock(&fifo_mutex);

	if (copied != sizeof(entry))
		return FIFO_INVALID;

	*value = entry.data;
	return FIFO_OK;
}

int fifo_peek(void)
{
	int result;
	int value;

	result = fifo_peek_value(&value);
	if (result != FIFO_OK)
		return result;

	return value;
}

int fifo_is_empty(void)
{
	int result;

	mutex_lock(&fifo_mutex);
	result = !fifo_initialized || kfifo_is_empty(&fifo_device.queue);
	mutex_unlock(&fifo_mutex);

	return result;
}

int fifo_is_full(void)
{
	int result;

	mutex_lock(&fifo_mutex);
	result = fifo_initialized &&
		 fifo_size_locked() >= fifo_device.max_size;
	mutex_unlock(&fifo_mutex);

	return result;
}

int fifo_size(void)
{
	int result;

	mutex_lock(&fifo_mutex);
	result = fifo_initialized ? fifo_size_locked() : 0;
	mutex_unlock(&fifo_mutex);

	return result;
}

int fifo_available(void)
{
	int result;

	mutex_lock(&fifo_mutex);
	if (!fifo_initialized) {
		result = 0;
	} else {
		result = fifo_device.max_size - fifo_size_locked();
	}
	mutex_unlock(&fifo_mutex);

	return result;
}

void fifo_clear(void)
{
	mutex_lock(&fifo_mutex);
	if (fifo_initialized)
		kfifo_reset(&fifo_device.queue);
	mutex_unlock(&fifo_mutex);
}
