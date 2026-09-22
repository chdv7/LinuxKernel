#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include "vmodem.h"

/*
 * Удаляет уже прочитанную часть response, освобождая место для новых ответов.
 * Функция вызывается только под vmodem->io_lock.
 */
static void compact_response(struct vmodem_device *vmodem)
{
	if (!vmodem->response_pos)
		return;

	if (vmodem->response_pos >= vmodem->response_len) {
		vmodem->response_pos = 0;
		vmodem->response_len = 0;
		return;
	}

	memmove(vmodem->response,
		vmodem->response + vmodem->response_pos,
		vmodem->response_len - vmodem->response_pos);
	vmodem->response_len -= vmodem->response_pos;
	vmodem->response_pos = 0;
}

/*
 * Добавляет байты в общий выходной поток виртуального COM-порта.
 * Функция вызывается под vmodem->io_lock. Выход не принадлежит конкретному
 * file descriptor: прочитать эти байты может любой reader /dev/vmodemN.
 */
static int append_output(struct vmodem_device *vmodem, const char *data,
			 size_t length)
{
	compact_response(vmodem);

	if (length > sizeof(vmodem->response) - vmodem->response_len)
		return -ENOSPC;

	memcpy(vmodem->response + vmodem->response_len, data, length);
	vmodem->response_len += length;

	/* minicom/read()/poll() могут ждать появления следующего символа. */
	wake_up_interruptible(&vmodem->response_wait);
	return 0;
}

/*
 * Добавляет асинхронное сообщение в тот же общий поток, который читает
 * minicom. Используется, например, для unsolicited result code NO CARRIER.
 * Вызывающий не должен держать state_lock: обычный write() берёт блокировки
 * в порядке io_lock -> state_lock, поэтому обратный порядок недопустим.
 */
int vmodem_emit_output(struct vmodem_device *vmodem, const char *data,
		       size_t length)
{
	int ret;

	mutex_lock(&vmodem->io_lock);
	ret = append_output(vmodem, data, length);
	mutex_unlock(&vmodem->io_lock);

	return ret;
}

/* Возвращает текущее состояние echo. */
static bool vmodem_echo_enabled(struct vmodem_device *vmodem)
{
	bool enabled;

	mutex_lock(&vmodem->state_lock);
	enabled = vmodem->state.echo_enabled;
	mutex_unlock(&vmodem->state_lock);

	return enabled;
}

/*
 * Немедленное echo одного принятого символа.
 * Backspace/DEL визуально стирают последний символ последовательностью
 * BS SPACE BS, которую понимают обычные терминальные программы.
 */
static int echo_input_byte(struct vmodem_device *vmodem, char byte)
{
	static const char erase[] = "\b \b";

	if (!vmodem_echo_enabled(vmodem))
		return 0;

	if (byte == '\b' || byte == 0x7f)
		return append_output(vmodem, erase, sizeof(erase) - 1);

	return append_output(vmodem, &byte, 1);
}

/* Выполняет одну завершённую AT-команду и добавляет ответ в общий поток. */
static int append_at_response(struct vmodem_device *vmodem,
			      const char *command)
{
	char response[VMODEM_AT_RESPONSE_MAX];
	size_t response_len;
	int ret;

	response_len = vmodem_process_command(vmodem, command, response,
					      sizeof(response));

	/*
	 * Стандартные текстовые result codes модема начинаются с CR/LF.
	 * При включённом echo перед этим уже был немедленно отражён входной CR.
	 */
	ret = append_output(vmodem, "\r\n", 2);
	if (ret)
		return ret;

	ret = append_output(vmodem, response, response_len);
	if (ret)
		return ret;

	pr_info("vmodem%u command: \"%s\"\n", vmodem->index, command);
	pr_info("vmodem%u response: \"\\r\\n%*pE\"\n", vmodem->index,
		(int)response_len, response);

	return 0;
}

/*
 * До подтверждения префикса AT входной мусор полностью игнорируется.
 * Это имитирует поведение модема: последовательность вроде "xxxAT" не
 * отображает "xxx" при включённом echo.
 *
 * Первую A/a нельзя немедленно вывести: пока не пришёл следующий символ,
 * неизвестно, действительно ли это начало AT. Поэтому A временно хранится
 * в at_prefix_a и выводится вместе с T/t после подтверждения префикса.
 */
static int consume_before_at_prefix(struct vmodem_device *vmodem, char byte)
{
	char prefix[2];
	int ret;

	/* LF и CR до найденного AT не образуют команду. */
	if (byte == '\n' || byte == '\r') {
		vmodem->at_prefix_pending = false;
		return 0;
	}

	/* Backspace/DEL до AT относится только к неотображаемому мусору. */
	if (byte == '\b' || byte == 0x7f) {
		vmodem->at_prefix_pending = false;
		return 0;
	}

	if (vmodem->at_prefix_pending) {
		if (byte == 'T' || byte == 't') {
			prefix[0] = vmodem->at_prefix_a;
			prefix[1] = byte;

			vmodem->command[0] = prefix[0];
			vmodem->command[1] = prefix[1];
			vmodem->command_len = 2;
			vmodem->command_active = true;
			vmodem->at_prefix_pending = false;

			if (!vmodem_echo_enabled(vmodem))
				return 0;

			/* После подтверждения AT показываем обе буквы сразу. */
			ret = append_output(vmodem, prefix, sizeof(prefix));
			return ret;
		}

		/* Новая A может быть началом следующего возможного префикса. */
		if (byte == 'A' || byte == 'a') {
			vmodem->at_prefix_a = byte;
			return 0;
		}

		vmodem->at_prefix_pending = false;
		return 0;
	}

	if (byte == 'A' || byte == 'a') {
		vmodem->at_prefix_a = byte;
		vmodem->at_prefix_pending = true;
	}

	return 0;
}

/*
 * Принимает один байт входного потока. Команда может прийти одним write(),
 * несколькими write() или полностью посимвольно.
 *
 * До подтверждённого AT-префикса мусор не отображается и не сохраняется.
 * Окончанием команды является CR ('\r'); LF ('\n') игнорируется.
 */
static int consume_input_byte(struct vmodem_device *vmodem, char byte)
{
	int ret;

	if (!vmodem->command_active)
		return consume_before_at_prefix(vmodem, byte);

	/* LF не является окончанием команды и не отражается обратно. */
	if (byte == '\n')
		return 0;

	/*
	 * Backspace (0x08) и DEL (0x7f) редактируют уже распознанную AT-строку.
	 * При echo терминалу отправляется BS SPACE BS.
	 */
	if (byte == '\b' || byte == 0x7f) {
		if (vmodem->discard_until_eol)
			return 0;

		if (!vmodem->command_len)
			return 0;

		--vmodem->command_len;
		ret = echo_input_byte(vmodem, byte);
		if (ret)
			return ret;

		/* Если пользователь стёр всю команду, снова ищем новый AT. */
		if (!vmodem->command_len) {
			vmodem->command_active = false;
			vmodem->at_prefix_pending = false;
		}

		return 0;
	}

	/* После найденного AT echo идёт сразу, символ за символом. */
	ret = echo_input_byte(vmodem, byte);
	if (ret)
		return ret;

	if (byte == '\r') {
		/* После переполнения игнорируем остаток строки до CR. */
		if (vmodem->discard_until_eol) {
			vmodem->discard_until_eol = false;
			vmodem->command_len = 0;
			vmodem->command_active = false;
			vmodem->at_prefix_pending = false;
			return 0;
		}

		if (!vmodem->command_len) {
			vmodem->command_active = false;
			return 0;
		}

		vmodem->command[vmodem->command_len] = '\0';
		ret = append_at_response(vmodem, vmodem->command);

		vmodem->command_len = 0;
		vmodem->command_active = false;
		vmodem->at_prefix_pending = false;
		return ret;
	}

	if (vmodem->discard_until_eol)
		return 0;

	/* Слишком длинную команду отвергаем и синхронизируемся на следующем CR. */
	if (vmodem->command_len >= sizeof(vmodem->command) - 1) {
		vmodem->command_len = 0;
		vmodem->discard_until_eol = true;
		return append_at_response(vmodem, "");
	}

	vmodem->command[vmodem->command_len++] = byte;
	return 0;
}

static int vmodem_open(struct inode *inode, struct file *file)
{
	unsigned int index = iminor(inode);
	struct vmodem_device *vmodem;

	if (index >= vmodem_count)
		return -ENODEV;

	/*
	 * private_data содержит только указатель на постоянный экземпляр модема.
	 * Парсер и выходной поток также принадлежат vmodemN и не теряются между
	 * несколькими open()/close(). Это ближе к поведению физического COM-порта.
	 */
	vmodem = &vmodems[index];
	file->private_data = vmodem;

	pr_debug("vmodem%u open file=%p\n", vmodem->index, file);
	return 0;
}

static int vmodem_release(struct inode *inode, struct file *file)
{
	struct vmodem_device *vmodem = file->private_data;

	if (vmodem)
		pr_debug("vmodem%u release file=%p\n", vmodem->index, file);

	file->private_data = NULL;
	return 0;
}

static bool response_available(struct vmodem_device *vmodem)
{
	return READ_ONCE(vmodem->response_pos) < READ_ONCE(vmodem->response_len);
}

static ssize_t vmodem_read(struct file *file, char __user *buffer, size_t count,
			   loff_t *offset)
{
	struct vmodem_device *vmodem = file->private_data;
	size_t available;
	size_t bytes;
	int ret;

	if (!count)
		return 0;

	for (;;) {
		mutex_lock(&vmodem->io_lock);

		if (vmodem->response_pos < vmodem->response_len)
			break;

		mutex_unlock(&vmodem->io_lock);

		/* Для non-blocking fd отсутствие данных означает EAGAIN, не EOF. */
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_interruptible(vmodem->response_wait,
					       response_available(vmodem));
		if (ret)
			return ret;
	}

	available = vmodem->response_len - vmodem->response_pos;
	bytes = min(count, available);
	if (copy_to_user(buffer, vmodem->response + vmodem->response_pos, bytes)) {
		mutex_unlock(&vmodem->io_lock);
		return -EFAULT;
	}

	vmodem->response_pos += bytes;
	if (vmodem->response_pos == vmodem->response_len) {
		vmodem->response_pos = 0;
		vmodem->response_len = 0;
	}

	mutex_unlock(&vmodem->io_lock);
	return bytes;
}

/* poll() сообщает minicom о готовности ответа к чтению. */
static __poll_t vmodem_poll(struct file *file, poll_table *wait)
{
	struct vmodem_device *vmodem = file->private_data;
	__poll_t mask = EPOLLOUT | EPOLLWRNORM;

	poll_wait(file, &vmodem->response_wait, wait);

	mutex_lock(&vmodem->io_lock);
	if (vmodem->response_pos < vmodem->response_len)
		mask |= EPOLLIN | EPOLLRDNORM;
	mutex_unlock(&vmodem->io_lock);

	return mask;
}

static ssize_t vmodem_write(struct file *file, const char __user *buffer,
			    size_t count, loff_t *offset)
{
	struct vmodem_device *vmodem = file->private_data;
	char debug_buffer[257];
	char chunk[128];
	size_t debug_len;
	size_t done = 0;
	size_t i;
	int ret;

	if (!count) {
		pr_info("vmodem%u write: count=0\n", vmodem->index);
		return 0;
	}

	/* Показываем фактически полученные от userspace байты. */
	debug_len = min_t(size_t, count, sizeof(debug_buffer) - 1);
	if (copy_from_user(debug_buffer, buffer, debug_len))
		return -EFAULT;
	debug_buffer[debug_len] = '\0';
	pr_info("vmodem%u write: count=%zu data=\"%*pE\"%s\n",
		vmodem->index, count, (int)debug_len, debug_buffer,
		count > debug_len ? " (truncated)" : "");

	mutex_lock(&vmodem->io_lock);

	/* Один write не обязан совпадать с одной AT-командой. */
	while (done < count) {
		size_t chunk_len = min_t(size_t, sizeof(chunk), count - done);

		if (copy_from_user(chunk, buffer + done, chunk_len)) {
			mutex_unlock(&vmodem->io_lock);
			return done ? (ssize_t)done : -EFAULT;
		}

		for (i = 0; i < chunk_len; ++i) {
			ret = consume_input_byte(vmodem, chunk[i]);
			if (ret) {
				mutex_unlock(&vmodem->io_lock);
				return done + i ? (ssize_t)(done + i) : ret;
			}
		}
		done += chunk_len;
	}

	mutex_unlock(&vmodem->io_lock);
	return count;
}

static const struct file_operations vmodem_fops = {
	.owner = THIS_MODULE,
	.open = vmodem_open,
	.release = vmodem_release,
	.read = vmodem_read,
	.write = vmodem_write,
	.poll = vmodem_poll,
	.unlocked_ioctl = vmodem_ioctl,
	.llseek = noop_llseek,
};

int vmodem_devices_create(void)
{
	unsigned int i;
	int ret;

	ret = alloc_chrdev_region(&vmodem_devt, 0, vmodem_count, VMODEM_NAME);
	if (ret)
		return ret;

	cdev_init(&vmodem_cdev, &vmodem_fops);
	vmodem_cdev.owner = THIS_MODULE;

	ret = cdev_add(&vmodem_cdev, vmodem_devt, vmodem_count);
	if (ret)
		goto err_unregister;

	vmodem_class = class_create(VMODEM_NAME);
	if (IS_ERR(vmodem_class)) {
		ret = PTR_ERR(vmodem_class);
		vmodem_class = NULL;
		goto err_cdev;
	}

	for (i = 0; i < vmodem_count; ++i) {
		vmodems[i].index = i;
		vmodem_state_init(&vmodems[i]);

		/* Инициализируем постоянный входной/выходной поток vmodemN. */
		mutex_init(&vmodems[i].io_lock);
		init_waitqueue_head(&vmodems[i].response_wait);
		vmodems[i].at_prefix_pending = false;
		vmodems[i].at_prefix_a = '\0';
		vmodems[i].command_active = false;
		vmodems[i].command_len = 0;
		vmodems[i].discard_until_eol = false;
		vmodems[i].response_len = 0;
		vmodems[i].response_pos = 0;

		vmodems[i].device = device_create(vmodem_class, NULL,
						  MKDEV(MAJOR(vmodem_devt), i),
						  &vmodems[i], "vmodem%u", i);
		if (IS_ERR(vmodems[i].device)) {
			ret = PTR_ERR(vmodems[i].device);
			vmodems[i].device = NULL;
			goto err_devices;
		}

		ret = vmodem_sysfs_create(&vmodems[i]);
		if (ret)
			goto err_current_device;
	}

	return 0;

err_current_device:
	device_destroy(vmodem_class, MKDEV(MAJOR(vmodem_devt), i));
	vmodems[i].device = NULL;
err_devices:
	while (i > 0) {
		--i;
		vmodem_sysfs_destroy(&vmodems[i]);
		device_destroy(vmodem_class, MKDEV(MAJOR(vmodem_devt), i));
		vmodems[i].device = NULL;
	}
	class_destroy(vmodem_class);
	vmodem_class = NULL;
err_cdev:
	cdev_del(&vmodem_cdev);
err_unregister:
	unregister_chrdev_region(vmodem_devt, vmodem_count);
	return ret;
}

void vmodem_devices_destroy(void)
{
	unsigned int i;

	for (i = 0; i < vmodem_count; ++i) {
		if (vmodems[i].device) {
			vmodem_sysfs_destroy(&vmodems[i]);
			device_destroy(vmodem_class,
				       MKDEV(MAJOR(vmodem_devt), i));
			vmodems[i].device = NULL;
		}
	}

	if (vmodem_class) {
		class_destroy(vmodem_class);
		vmodem_class = NULL;
	}

	cdev_del(&vmodem_cdev);
	unregister_chrdev_region(vmodem_devt, vmodem_count);
}
