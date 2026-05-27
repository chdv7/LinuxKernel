#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#define MAX_LEN  64

// str_buf  read only

static char buf [MAX_LEN];
static char* str_buf = buf;
module_param(str_buf, charp, 0444);
MODULE_PARM_DESC(str_buf, "A character string");



// index  read/write

static int idx = 0;
static int int_val_set(const char *val, const struct kernel_param *kp){
    int int_val = 0;
    int ret = kstrtoint(val, 10, &int_val);
    if(ret){
        pr_err("kstrtoint error <%s>\n", val);
        return ret;
    }

    if ( int_val < 0 || int_val >= sizeof (buf)-1 ) {
        pr_err("Wrong idx: %d\n", idx);
        return -EINVAL;
    }
    idx = int_val;
    pr_info("set_idx = %d\n", idx);
    return 0;
}

static int int_val_get(char *val, const struct kernel_param *kp){
    pr_info("get_idx = %d\n", idx);
    return sprintf(val, "%d\n", idx);
}


static const struct kernel_param_ops int_val_params ={
    .set = int_val_set,
    .get = int_val_get,
};

module_param_cb(idx, &int_val_params, &idx, 0664);
MODULE_PARM_DESC(idx, "Index");

static int ch_val_set(const char *val, const struct kernel_param *kp){
   if (!isprint(*val)){
       pr_err("Wrong character\n");
       return -EINVAL;
   }

    pr_info("set_val = %02X (%c)\n", (unsigned int)*val, *val);

    buf[idx] = *val;
    return 0;
}

static int ch_val_get(char *val, const struct kernel_param *kp){
    pr_info("get_val = %02X (%c)\n", (unsigned int)buf[idx], buf[idx]);
    return sprintf(val, "%c\n", buf[idx]);
    return 0;
}


static const struct kernel_param_ops ch_val_params ={
    .set = ch_val_set,
    .get = ch_val_get,
};



// ch_val write only


char* ch_val = "X";
module_param_cb(ch_val, &ch_val_params, &idx, 0664);
MODULE_PARM_DESC(ch_val, "value ");



static int __init hello_init(void){
    memset (buf, 0, sizeof(buf));
    pr_info("HW-2 chdv started\n");
    return 0;
}

static void __exit hello_exit(void){
    pr_info("Goodbye HW-2 ched\n");
}

module_init(hello_init);
module_exit(hello_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dmitry Chuprov");
MODULE_DESCRIPTION("Home work 2");
MODULE_VERSION("1.0");