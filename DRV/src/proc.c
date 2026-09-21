#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "vmodem.h"

static struct proc_dir_entry *vmodem_proc_entry;

static int vmodem_proc_show(struct seq_file *m, void *v)
{
	unsigned int i;

	seq_printf(m, "driver=%s\n", VMODEM_NAME);
	seq_printf(m, "modems=%u\n", vmodem_count);
	seq_printf(m, "major=%u\n", MAJOR(vmodem_devt));

	for (i = 0; i < vmodem_count; ++i)
		seq_printf(m, "device=/dev/vmodem%u minor=%u\n", i, i);

	return 0;
}

static int vmodem_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, vmodem_proc_show, NULL);
}

static const struct proc_ops vmodem_proc_ops = {
	.proc_open = vmodem_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int vmodem_proc_create(void)
{
	vmodem_proc_entry = proc_create(VMODEM_NAME, 0444, NULL,
					&vmodem_proc_ops);
	if (!vmodem_proc_entry)
		return -ENOMEM;

	return 0;
}

void vmodem_proc_destroy(void)
{
	if (vmodem_proc_entry) {
		proc_remove(vmodem_proc_entry);
		vmodem_proc_entry = NULL;
	}
}
