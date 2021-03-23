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
MODULE_DESCRIPTION("WS2811 leds driver");
MODULE_VERSION("1.0");


// for delay
#define T0H_TIME 50
#define T0L_TIME 1950
#define T1H_TIME 1500
#define T1L_TIME 500

/*
#define T0H_TIME 250-150
#define T0L_TIME 1000
#define T1H_TIME 600-150
#define T1L_TIME 650
*/

#define CLASS_NAME  "ws2811"

static u8 pin = 0;
static u16 leds = 50;
static u8* leds_data = NULL;
static char* device_name = "leds";
module_param(pin, byte, 0);
MODULE_PARM_DESC(pin,"pin number");
module_param(leds, short, 0);
MODULE_PARM_DESC(leds,"number of leds (default 50)");
module_param(device_name, charp, 0);
MODULE_PARM_DESC(device_name,"name of pseudofile (default \"leds\")");
static int majorNumber = -1;
static struct class*  ws2811Class  = NULL;
static struct device* ws2811Device = NULL;
static struct gpio_desc* pin_desc = NULL;
DEFINE_SPINLOCK(lock);

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
    u8 bit, i;
    u16 byte;
    unsigned long flags;
    //u64 t;
    spin_lock_irqsave(&lock, flags);
    gpiod_set_value(pin_desc, 0);
    for (byte = 0; byte < 3 * leds; byte++)
    {
        for (bit = 0; bit < 8; bit++)
        {            
            for (i = 0; i < 50; i++)
                ndelay(10); // align timer
            if (leds_data[byte] & (1<<(7-bit)))
            {
                gpiod_set_value(pin_desc, 1);
                ndelay(T1H_TIME);
                gpiod_set_value(pin_desc, 0);
                ndelay(T1L_TIME);
            } else {
                gpiod_set_value(pin_desc, 1);
                ndelay(T0H_TIME);
                gpiod_set_value(pin_desc, 0);
                ndelay(T0L_TIME);
            }
            

            //for (i = 0; i < 50; i++)
            //ndelay(10); // align timer
            /*
            t = ktime_get_ns();
            t += 500;
            while (ktime_get_ns() < t) ;
            if (leds_data[byte] & (1<<(7-bit)))
            {
                t = ktime_get_ns();
                gpiod_set_value(pin_desc, 1);
                t += (T1H_TIME);
                while (ktime_get_ns() < t) ;
                t = ktime_get_ns();
                gpiod_set_value(pin_desc, 0);
                t += ((T1L_TIME) - 500);
                while (ktime_get_ns() < t) ;
            } else {
                t = ktime_get_ns();
                gpiod_set_value(pin_desc, 1);
                t += (T0H_TIME);
                while (ktime_get_ns() < t) ;
                t = ktime_get_ns();
                gpiod_set_value(pin_desc, 0);
                t += ((T0L_TIME) - 500);
                while (ktime_get_ns() < t) ;
            }
            */
        }
    }
    spin_unlock_irqrestore(&lock, flags);
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
        unregister_chrdev(majorNumber, device_name);            // unregister the major number
    if (!IS_ERR_OR_NULL(pin_desc))
        gpiod_put(pin_desc);
    kfree(leds_data);
}

static __init int ws2811_init(void)
{
    int r;
    pin_desc = gpio_to_desc(pin);
    if (IS_ERR(pin_desc))
    {
        printk(KERN_ERR "WS2811: gpiod_request result: %ld\n", PTR_ERR(pin_desc));
        ws2811_free();
        return -1;
    }
    r = gpiod_direction_output(pin_desc, 0);
    if (r)
    {
        printk(KERN_ERR "WS2811: gpiod_request result: %d\n", r);
        ws2811_free();
        return -1;
    }
    leds_data = kzalloc(3 * leds, GFP_KERNEL);
    if (!leds_data)
    {
        printk(KERN_ERR "WS2811: can't allocate memory for leds data\n");
        ws2811_free();
        return -1;
    }
    majorNumber = register_chrdev(0, device_name, &fops);
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
    ws2811Device = device_create(ws2811Class, NULL, MKDEV(majorNumber, 0), NULL, device_name);
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
    loff_t max_pos = 3 * leds;
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
    loff_t max_pos = 3 * leds;
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
    loff_t max_pos = 3 * leds;
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
