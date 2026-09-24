#ifndef SRC_VMODEM_H_
#define SRC_VMODEM_H_

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#define VMODEM_NAME "vmodem"
#define VMODEM_MAX_DEVICES 16U
#define VMODEM_DEFAULT_DEVICES 1U

/* Максимальная длина одной AT-команды вместе с завершающим '\0'. */
#define VMODEM_COMMAND_MAX 256U

/* Общий выходной поток одного виртуального модема. */
#define VMODEM_RESPONSE_MAX 4096U

/* Максимальный ответ на одну AT-команду. */
#define VMODEM_AT_RESPONSE_MAX 512U

#define VMODEM_OPERATOR_MAX 32U
#define VMODEM_NUMBER_MAX 32U
#define VMODEM_IMEI_MAX 16U
#define VMODEM_IMSI_MAX 16U

/* CSQ: 0..31 — известный уровень сигнала, 99 — неизвестный. */
#define VMODEM_DEFAULT_SIGNAL 20U
#define VMODEM_UNKNOWN_SIGNAL 99U

#define VMODEM_IOCTL_MAGIC 'V'

/*
 * Представление состояния, которое можно безопасно передать в userspace
 * через ioctl(). Здесь используются типы фиксированной ширины.
 */
struct vmodem_user_state {
	__u32 index;
	__u32 echo_enabled;
	__u32 registered;
	__u32 signal_level;
	__u32 sim_ready;
	__u32 call_state;
	__u32 connected;
	char operator_name[VMODEM_OPERATOR_MAX];
	char dial_number[VMODEM_NUMBER_MAX];
	char imei[VMODEM_IMEI_MAX];
	char imsi[VMODEM_IMSI_MAX];
};

#define VMODEM_IOCTL_GET_STATE \
	_IOR(VMODEM_IOCTL_MAGIC, 1, struct vmodem_user_state)
#define VMODEM_IOCTL_RESET _IO(VMODEM_IOCTL_MAGIC, 2)

enum vmodem_call_state {
	VMODEM_CALL_IDLE = 0,
	VMODEM_CALL_INCOMING,
	VMODEM_CALL_ACTIVE,
};

/*
 * Постоянное состояние одного виртуального модема.
 * Оно принадлежит устройству, а не struct file, поэтому close()/open()
 * не сбрасывают настройки модема.
 */
struct vmodem_state {
	bool echo_enabled;
	bool registered;
	unsigned int signal_level;
	bool sim_ready;
	enum vmodem_call_state call_state;
	bool call_incoming;
	bool connected;
	char operator_name[VMODEM_OPERATOR_MAX];
	char dial_number[VMODEM_NUMBER_MAX];
	char imei[VMODEM_IMEI_MAX];
	char imsi[VMODEM_IMSI_MAX];
};

/* Один экземпляр /dev/vmodemN. */
struct vmodem_device {
	unsigned int index;
	struct device *device;

	/* Защищает всё содержимое state. */
	struct mutex state_lock;
	struct vmodem_state state;

	/*
	 * Входной поток и ответы принадлежат самому виртуальному COM-порту,
	 * а не отдельному open(). Это позволяет терминальным программам
	 * передавать команду посимвольно и использовать разные file descriptor
	 * для чтения и записи, не теряя уже накопленные байты.
	 */
	struct mutex io_lock;

	/*
	 * До подтверждения префикса AT входной мусор не попадает в command[].
	 * При включённом echo каждая A/a отображается сразу и запоминается как
	 * возможное начало AT. Остальной мусор до AT остаётся невидимым.
	 * При выключенном echo не отображается ничего, включая сам префикс AT.
	 */
	bool at_prefix_pending;
	char at_prefix_a;
	bool command_active;

	/* Команда собирается сюда после подтверждения префикса AT. */
	char command[VMODEM_COMMAND_MAX];
	size_t command_len;
	bool discard_until_eol;

	/* read()/poll() ждут здесь любых выходных данных: echo или ответа. */
	wait_queue_head_t response_wait;

	/* Общий выходной поток виртуального COM-порта; не привязан к open fd. */
	char response[VMODEM_RESPONSE_MAX];
	size_t response_len;
	size_t response_pos;
};

extern unsigned int vmodem_count;
extern dev_t vmodem_devt;
extern struct cdev vmodem_cdev;
extern struct class *vmodem_class;
extern struct vmodem_device vmodems[VMODEM_MAX_DEVICES];

int vmodem_devices_create(void);
void vmodem_devices_destroy(void);

void vmodem_state_init(struct vmodem_device *vmodem);
void vmodem_state_reset(struct vmodem_device *vmodem);
void vmodem_state_reset_locked(struct vmodem_device *vmodem);
const char *vmodem_call_state_name(enum vmodem_call_state state);

size_t vmodem_process_command(struct vmodem_device *vmodem, const char *command,
			      char *response, size_t response_size);

long vmodem_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

/* Добавляет асинхронный текст в общий выходной поток /dev/vmodemN. */
int vmodem_emit_output(struct vmodem_device *vmodem, const char *data,
		       size_t length);

int vmodem_proc_create(void);
void vmodem_proc_destroy(void);

int vmodem_sysfs_create(struct vmodem_device *vmodem);
void vmodem_sysfs_destroy(struct vmodem_device *vmodem);

#endif /* SRC_VMODEM_H_ */
