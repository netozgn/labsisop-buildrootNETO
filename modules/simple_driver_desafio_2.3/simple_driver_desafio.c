#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/string.h>

#define DEVICE_NAME "simple_driver_desafio"
#define CLASS_NAME  "simple_driver_desafio_class"
#define MAX_DATA 1024
#define NUM_ROUNDS 32

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author");
MODULE_DESCRIPTION("Simple XTEA char driver with key params");
MODULE_VERSION("0.2");

static int majorNumber;
static struct class*  xteaClass  = NULL;
static struct device* xteaDevice = NULL;

struct msg_node {
    struct list_head list;
    char *data;
    size_t len;
};
static LIST_HEAD(msg_list);

static uint32_t tea_key[4];
static char result_buf[MAX_DATA];
static size_t result_size = 0;

/* ----------- Module Params (como string) ------------ */
static char *key0 = "00000000";
static char *key1 = "00000000";
static char *key2 = "00000000";
static char *key3 = "00000000";

module_param(key0, charp, 0444);
MODULE_PARM_DESC(key0, "First 32-bit word of XTEA key in hex");

module_param(key1, charp, 0444);
MODULE_PARM_DESC(key1, "Second 32-bit word of XTEA key in hex");

module_param(key2, charp, 0444);
MODULE_PARM_DESC(key2, "Third 32-bit word of XTEA key in hex");

module_param(key3, charp, 0444);
MODULE_PARM_DESC(key3, "Fourth 32-bit word of XTEA key in hex");

/* ---------------- XTEA ---------------- */
static void xtea_encipher(uint32_t v[2], const uint32_t key[4]) {
    uint32_t y, z, sum, delta;
    int i;
    y = v[0]; z = v[1]; sum = 0; delta = 0x9E3779B9;
    for (i = 0; i < NUM_ROUNDS; i++) {
        y += ((z << 4 ^ z >> 5) + z) ^ (sum + key[sum & 3]);
        sum += delta;
        z += ((y << 4 ^ y >> 5) + y) ^ (sum + key[(sum >> 11) & 3]);
    }
    v[0] = y; v[1] = z;
}

static void xtea_decipher(uint32_t v[2], const uint32_t key[4]) {
    uint32_t y, z, sum, delta;
    int i;
    y = v[0]; z = v[1]; delta = 0x9E3779B9; sum = delta * NUM_ROUNDS;
    for (i = 0; i < NUM_ROUNDS; i++) {
        z -= ((y << 4 ^ y >> 5) + y) ^ (sum + key[(sum >> 11) & 3]);
        sum -= delta;
        y -= ((z << 4 ^ z >> 5) + z) ^ (sum + key[sum & 3]);
    }
    v[0] = y; v[1] = z;
}

/* ---------- HEX <-> BYTE helpers ---------- */
static int hex2nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if(c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int my_hex2bin(const char *hex, uint8_t *out, size_t outlen) {
    size_t i;
    int hi, lo;
    for (i = 0; i < outlen; i++) {
        hi = hex2nibble(hex[2*i]);
        lo = hex2nibble(hex[2*i+1]);
        if (hi < 0 || lo < 0) return -EINVAL;
        out[i] = (hi << 4) | lo;
    }
    return 0;
}

static void my_bin2hex(const uint8_t *in, size_t inlen, char *out) {
    static const char hexchars[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < inlen; i++) {
        out[2*i] = hexchars[in[i] >> 4];
        out[2*i+1] = hexchars[in[i] & 0xF];
    }
    out[2*inlen] = '\0';
}

/* ---------- Processa string hex com XTEA ---------- */
static int process_tea(const char *data_hex, char *result, int enc) {
    size_t len;
    size_t i;
    uint8_t buf[8];
    uint32_t v[2];

    len = strlen(data_hex);
    if (len % 16 != 0) return -EINVAL;

    for (i = 0; i < len / 16; i++) {
        if (my_hex2bin(&data_hex[i*16], buf, 8)) return -EINVAL;

        v[0] = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
        v[1] = (buf[4]<<24)|(buf[5]<<16)|(buf[6]<<8)|buf[7];

        if (enc) xtea_encipher(v, tea_key);
        else xtea_decipher(v, tea_key);

        buf[0] = v[0]>>24; buf[1] = (v[0]>>16)&0xFF; buf[2] = (v[0]>>8)&0xFF; buf[3] = v[0]&0xFF;
        buf[4] = v[1]>>24; buf[5] = (v[1]>>16)&0xFF; buf[6] = (v[1]>>8)&0xFF; buf[7] = v[1]&0xFF;

        my_bin2hex(buf, 8, &result[i*16]);
    }

    return 0;
}

/* ---------- FOPS ---------- */
static int dev_open(struct inode *inodep, struct file *filep) { 
    printk(KERN_INFO "XTEA Driver opened\n"); 
    return 0; 
}

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    size_t ret;
    if(result_size == 0) return 0;
    if(copy_to_user(buffer, result_buf, result_size)) return -EFAULT;
    ret = result_size;
    result_size = 0;
    return ret;
}

static ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset) {
    char *kbuf;
    char op[4], data_hex[MAX_DATA];
    int parsed;
    int enc;

    kbuf = kmalloc(len+1, GFP_KERNEL);
    if(!kbuf) return -ENOMEM;
    if(copy_from_user(kbuf, buffer, len)) { kfree(kbuf); return -EFAULT; }
    kbuf[len] = '\0';

    parsed = sscanf(kbuf, "%3s %s", op, data_hex);  // só operação + dados
    kfree(kbuf);
    if(parsed < 2) return -EINVAL;

    enc = (strncmp(op,"enc",3)==0);

    if(process_tea(data_hex, result_buf, enc)) return -EINVAL;
    result_size = strlen(result_buf);

    printk(KERN_INFO "XTEA Driver: processed %zu bytes\n", strlen(data_hex)/2);
    return len;
}

static int dev_release(struct inode *inodep, struct file *filep) { 
    printk(KERN_INFO "XTEA Driver closed\n"); 
    return 0; 
}

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release
};

/* ---------- INIT / EXIT ---------- */
static int __init xtea_init(void) {
    if (kstrtouint(key0, 16, &tea_key[0]) ||
        kstrtouint(key1, 16, &tea_key[1]) ||
        kstrtouint(key2, 16, &tea_key[2]) ||
        kstrtouint(key3, 16, &tea_key[3])) {
        printk(KERN_ERR "XTEA Driver: erro convertendo chaves\n");
        return -EINVAL;
    }

    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber < 0) {
        printk(KERN_ALERT "XTEA failed to register a major number\n");
        return majorNumber;
    }

    xteaClass = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(xteaClass)) {
        unregister_chrdev(majorNumber, DEVICE_NAME);
        return PTR_ERR(xteaClass);
    }

    xteaDevice = device_create(xteaClass, NULL, MKDEV(majorNumber,0), NULL, DEVICE_NAME);
    if (IS_ERR(xteaDevice)) {
        class_destroy(xteaClass);
        unregister_chrdev(majorNumber, DEVICE_NAME);
        return PTR_ERR(xteaDevice);
    }

    printk(KERN_INFO "XTEA Driver loaded with key: %08x %08x %08x %08x\n",
           tea_key[0], tea_key[1], tea_key[2], tea_key[3]);
    return 0;
}

static void __exit xtea_exit(void) {
    device_destroy(xteaClass, MKDEV(majorNumber,0));
    class_unregister(xteaClass);
    class_destroy(xteaClass);
    unregister_chrdev(majorNumber, DEVICE_NAME);

    printk(KERN_INFO "XTEA Driver unloaded\n");
}

module_init(xtea_init);
module_exit(xtea_exit);