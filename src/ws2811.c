#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey 'Cluster' Avdyukhin");
MODULE_DESCRIPTION("WS2811 leds driver for Omega2");
MODULE_VERSION("2.0");


// WS2811 timings
#define T0H_TIME 200
#define T1H_TIME 1200
#define CYCLE_TIME 2000
#define RES_TIME 50000

#define CLASS_NAME  "ws2811"
#define DEVICE_NAME "ws2811"

#define GPIO_BASE     0x10000600
#define GPIO_SIZE     0x00000050
#define GPIO_CTRL_0   0x00000000
#define GPIO_CTRL_0   0x00000000
#define GPIO_CTRL_1   0x00000004
#define GPIO_CTRL_2   0x00000008
#define GPIO_POL_0    0x00000010
#define GPIO_POL_1    0x00000014
#define GPIO_POL_2    0x00000018
#define GPIO_DATA_0   0x00000020
#define GPIO_DATA_1   0x00000024
#define GPIO_DATA_2   0x00000028
#define GPIO_DSET_0   0x00000030
#define GPIO_DSET_1   0x00000034
#define GPIO_DSET_2   0x00000038
#define GPIO_DCLR_0   0x00000040
#define GPIO_DCLR_1   0x00000044
#define GPIO_DCLR_2   0x00000048
#define REG_WRITE(addr, value) iowrite32(value, gpio_regs + addr)
#define REG_READ(addr) ioread32(gpio_regs + addr)

static u8 pins[32] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static int pin_count = 0;
static u16 led_count = 50;
static u8* leds_data = NULL;
module_param_array(pins, byte, &pin_count, 0);
MODULE_PARM_DESC(pins,"pin number(s)");
module_param(led_count, short, 0);
MODULE_PARM_DESC(led_count,"number of leds per pin (default 50)");
static int majorNumber = -1;
static struct class*  ws2811Class  = NULL;
static struct device* ws2811Device = NULL;
void* gpio_regs = NULL;
u32 mask_0, mask_1;
DEFINE_SPINLOCK(lock);
static u64 last_transfer_time_ns = 0;

// The prototype functions for the character driver -- must come before the struct definition
static int     dev_open(struct inode *, struct file *);
static int     dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static loff_t  dev_llseek(struct file *file,loff_t offset, int orig);
 
static struct file_operations fops =
{
   .open = dev_open,
   .read = dev_read,
   .write = dev_write,
   .release = dev_release,
   .llseek = dev_llseek,
};
 
static void sync_leds(void)
{
    u8 bit, pin;
    u16 byte;
    unsigned long flags;
    u32 zero_0, zero_1;
    u64 now;
    
    // Clear all pins
    REG_WRITE(GPIO_DCLR_0, mask_0);
    REG_WRITE(GPIO_DCLR_1, mask_1);
    // Reset time if need
    now = ktime_to_ns(ktime_get_boottime());
    if (now - last_transfer_time_ns < RES_TIME)
        ndelay(RES_TIME - (now - last_transfer_time_ns));
    ndelay(CYCLE_TIME); // for align
    spin_lock_irqsave(&lock, flags);
    for (byte = 0; byte < 3 * led_count; byte++)
    {
        for (bit = 0; bit < 8; bit++)
        {            
            // Prepare values
            zero_0 = 0;
            zero_1 = 0;
            for (pin = 0; pin < pin_count; pin++)
            {
                if (!(leds_data[byte + pin * led_count * 3] & (1<<(7-bit))))
                {
                    if (pins[pin] < 32)
                        zero_0 |= 1 << pins[pin];
                    else if (pins[pin] < 64)
                        zero_1 |= 1 << (pins[pin] - 32);
                }
            }
            // Set all
            REG_WRITE(GPIO_DSET_0, mask_0);
            REG_WRITE(GPIO_DSET_1, mask_1);
            ndelay(T0H_TIME); // 50
            // Clear for zero bits
            REG_WRITE(GPIO_DCLR_0, zero_0);
            REG_WRITE(GPIO_DCLR_1, zero_1);
            ndelay(T1H_TIME - T0H_TIME);
            // Clear all
            REG_WRITE(GPIO_DCLR_0, mask_0);
            REG_WRITE(GPIO_DCLR_1, mask_1);
            ndelay(CYCLE_TIME - T1H_TIME);
        }
    }
    spin_unlock_irqrestore(&lock, flags);
    last_transfer_time_ns = ktime_to_ns(ktime_get_boottime());
}

static void ws2811_free(void)
{
    if (ws2811Class)
    {
        device_destroy(ws2811Class, MKDEV(majorNumber, 0));     // remove the device
        class_unregister(ws2811Class);                          // unregister the device class
        class_destroy(ws2811Class);                             // remove the device class
    }
    if (majorNumber >= 0)
        unregister_chrdev(majorNumber, DEVICE_NAME);            // unregister the major number
    if (gpio_regs != NULL)
        iounmap(gpio_regs);                                     // unmap registers
    kfree(leds_data);                                           // free allocated memory
}

static __init int ws2811_init(void)
{
    int pin;
    // Map Omega2's registers
    gpio_regs = ioremap_nocache(GPIO_BASE, GPIO_SIZE);
    if (gpio_regs == NULL)
    {
        printk(KERN_ERR "WS2811: failed to map physical memory\n");
        ws2811_free();
        return -1;
    }
    // Calculate mask for pins
    mask_0 = 0;
    mask_1 = 0;
    for (pin = 0; pin < pin_count; pin++)
    {
        if (pins[pin] < 32) 
            mask_0 |= 1 << pins[pin];
        else if (pins[pin] < 64) 
            mask_1 |= 1 << (pins[pin] - 32);
    }
    REG_WRITE(GPIO_CTRL_0, REG_READ(GPIO_CTRL_0) | mask_0); // output
    REG_WRITE(GPIO_CTRL_1, REG_READ(GPIO_CTRL_1) | mask_1); // output

    // Allocate memory
    leds_data = kzalloc(3 * led_count * pin_count, GFP_KERNEL);
    if (!leds_data)
    {
        printk(KERN_ERR "WS2811: can't allocate memory for leds data\n");
        ws2811_free();
        return -1;
    }

    // Register character device and request major number
    majorNumber = register_chrdev(0, DEVICE_NAME, &fops);
    if (majorNumber<0)
    {
        printk(KERN_ERR "WS2811: failed to register a major number\n");
        ws2811_free();
        return -1;
    }

    // Register the device class
    ws2811Class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(ws2811Class))
    {
        ws2811_free();
        printk(KERN_ERR "WS2811: failed to register device class\n");
        return -1;
    }

    // Register the device driver
    ws2811Device = device_create(ws2811Class, NULL, MKDEV(majorNumber, 0), NULL, DEVICE_NAME);
    if (IS_ERR(ws2811Device))
    {
        ws2811_free();
        printk(KERN_ERR "WS2811: failed to create the device\n");
        return -1;
    }

    sync_leds();
    printk(KERN_INFO "WS2811: driver started\n");
    return 0;
}

static void __exit ws2811_exit(void)
{
    ws2811_free();    
    printk(KERN_INFO "WS2811: driver stopped\n");
}

static int dev_open(struct inode *inodep, struct file *filep)
{
   return 0;
}
 
static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    loff_t max_pos = 3 * led_count * pin_count;
    ssize_t r = 0;
    while ((*offset < max_pos) && (r < len))
    {
        if (put_user(leds_data[*offset], buffer))
             return -EFAULT;
        (*offset)++;
        r++;
        buffer++;
    }
    return r;
}
 
static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    loff_t max_pos = 3 * led_count * pin_count;
    ssize_t r = 0;
    while (r < len)
    {
        if (*offset >= max_pos)
        {
            if (r) return r;
            return -EFAULT;
        }
        if (get_user(leds_data[*offset], buffer))
        {
            return -EFAULT;
        }
        (*offset)++;
        r++;
        buffer++;
    }
    sync_leds();
    return r;
}

static loff_t dev_llseek(struct file *file, loff_t offset, int orig)
{
    loff_t max_pos = 3 * led_count * pin_count;
    loff_t new_pos = 0;
    switch (orig)
    {
        case 0: /* SEEK_SET: */
                new_pos = offset;
                break;
        case 1: /* SEEK_CUR: */
                new_pos = file->f_pos + offset;
                break;
        case 2: /* SEEK_END: */
                new_pos = max_pos + offset;
                break;
        default:
                return -EINVAL;
    }
    if ( new_pos > max_pos ) return -EINVAL;
    if ( new_pos < 0 ) return -EINVAL;
    file->f_pos = new_pos;
    return new_pos;
}
 
static int dev_release(struct inode *inodep, struct file *filep)
{
   return 0;
}

module_init(ws2811_init);
module_exit(ws2811_exit);
