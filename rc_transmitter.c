#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include "rc_transmitter.h"

module_param(gpio_pin, uint, 0);

/* Format for protocol definitions:
 * {pulselength, Sync bit, "0" bit, "1" bit}
 *
 * pulselength: pulse length in microseconds, e.g. 350
 * Sync bit: {1, 31} means 1 high pulse and 31 low pulses
 *     (perceived as a 31*pulselength long pulse, total length of sync bit is
 *     32*pulselength microseconds), i.e:
 *      _
 *     | |_______________________________ (don't count the vertical bars)
 * "0" bit: waveform for a data bit of value "0", {1, 3} means 1 high pulse
 *     and 3 low pulses, total length (1+3)*pulselength, i.e:
 *      _
 *     | |___
 * "1" bit: waveform for a data bit of value "1", e.g. {3,1}:
 *      ___
 *     |   |_
 *
 * These are combined to form Tri-State bits when sending or receiving codes.
 */

/* Protocol 1 */
#define PULSE_LENGTH_MICROS (unsigned long)350
#define SYNC_BIT_HIGH_PULSES 1
#define SYNC_BIT_LOW_PULSES 31
#define ONE_HIGH_PULSES 3
#define ONE_LOW_PULSES 1
#define ZERO_HIGH_PULSES 1
#define ZERO_LOW_PULSES 3


#define DEVICE_NAME "rc_transmitter"
static dev_t device_major = (dev_t)-1;

static ssize_t device_write(struct file * file_ptr, const char * buffer, size_t buffer_length, loff_t * offset);
static struct file_operations fops = {
        .write = device_write,
};
static struct miscdevice misc_device = {
        .name = DEVICE_NAME,
        .minor = 0,
        .fops = &fops,
        .mode = 0220,
};

static DEFINE_SPINLOCK(transmit_lock);

static int __init examplemod_init(void) {
    int ret;
    ret = alloc_chrdev_region(&device_major, 1, 1, DEVICE_NAME);
    if (ret) {
        printk(KERN_ALERT "alloc_chrdev_region errno %d", ret);
        return ret;
    }

    fops.owner = THIS_MODULE;
    ret = misc_register(&misc_device);
    if (ret) {
        printk(KERN_ALERT "misc_register errno %d", ret);
        goto err_cdev_del;
    }

    ret = gpio_direction_output(gpio_pin, 0);
    if (ret) {
        printk(KERN_ALERT "gpio_direction_output(%d) errno %d\n", gpio_pin, ret);
        goto err_destroy_device;
    }
    return 0;

err_destroy_device:
    misc_deregister(&misc_device);
err_cdev_del:
    unregister_chrdev_region(device_major, 1);
    return ret;
}

static void __exit examplemod_exit(void) {
    unsigned long irq_flags;
    misc_deregister(&misc_device);
    unregister_chrdev_region(device_major, 1);
    spin_lock_irqsave(&transmit_lock, irq_flags);
    gpio_set_value(gpio_pin, 0);
    gpio_free(gpio_pin);
    spin_unlock_irqrestore(&transmit_lock, irq_flags);
}

module_init(examplemod_init);
module_exit(examplemod_exit);


int send_message(const u32 message, const u8 bits, s32 repeat) {
    unsigned long irq_flags;
    u32 last_signal_delay;
    u32* signal_delays;
    u32* signal_delays_cursor;
    u32* signal_delays_end;
    ktime_t next_time, start_time, end_time, current_time;
    const size_t signal_delays_count = bits * 2 + 1;
    u32 message_copy = message;
    int bit;
    if (0 == bits) {
        return -EINVAL;
    }
    signal_delays = kmalloc(signal_delays_count * sizeof(*signal_delays), GFP_KERNEL);
    if (NULL == signal_delays) {
        return -ENOMEM;
    }
    last_signal_delay = SYNC_BIT_LOW_PULSES * PULSE_LENGTH_MICROS;
    signal_delays_cursor = signal_delays_end = signal_delays + signal_delays_count;
    *--signal_delays_cursor = SYNC_BIT_HIGH_PULSES * PULSE_LENGTH_MICROS;
    while (signal_delays_cursor > signal_delays) {
        bit = message_copy & 1;
        if (bit) {
            *--signal_delays_cursor = ONE_LOW_PULSES * PULSE_LENGTH_MICROS;
            *--signal_delays_cursor = ONE_HIGH_PULSES * PULSE_LENGTH_MICROS;
        } else {
            *--signal_delays_cursor = ZERO_LOW_PULSES * PULSE_LENGTH_MICROS;
            *--signal_delays_cursor = ZERO_HIGH_PULSES * PULSE_LENGTH_MICROS;
        }
        message_copy >>= 1;
    }

    while (repeat-- > 0) {
        signal_delays_cursor = signal_delays;
        bit = 1;
        spin_lock_irqsave(&transmit_lock, irq_flags);
        gpio_set_value(gpio_pin, 0);
        start_time = next_time = ktime_get_boottime();
        while (true) {
            current_time = ktime_get_boottime();
            if (ktime_compare(next_time, current_time) <= 0) {
                if (signal_delays_cursor >= signal_delays_end) {
                    break;
                }
                gpio_set_value(gpio_pin, bit);
                bit = bit ? 0 : 1;
                next_time = ktime_add_us(next_time, *signal_delays_cursor);
                ++signal_delays_cursor;
            }
        }
        gpio_set_value(gpio_pin, 0);
        spin_unlock_irqrestore(&transmit_lock, irq_flags);
        usleep_range((u64) last_signal_delay, (u64) last_signal_delay + (PULSE_LENGTH_MICROS));
        end_time = ktime_get_boottime();
    }
    kfree(signal_delays);
    return 0;
}


static ssize_t device_write(struct file * file_ptr, const char * buffer, size_t buffer_length, loff_t * offset) {
    int ret;
    char kern_buf[128] = {0};
    unsigned message = 0;
    unsigned bits = 0;
    unsigned repeat = 0;
    ret = copy_from_user(kern_buf, buffer, min(sizeof(kern_buf) - 1, buffer_length));
    if (ret) {
        printk(KERN_ALERT "Failed copy_from_user in %s, errno %d", DEVICE_NAME, ret);
        return -EFAULT;
    }
    kern_buf[sizeof(kern_buf) - 1] = 0;
    sscanf(kern_buf, "%x %x %x", &message, &bits, &repeat);
    if (message == 0 || bits == 0 || bits > 32 || repeat == 0) {
        printk(KERN_WARNING "Written wrong message into %s."
                       " Expected text in format \"%%x %%x %%x\" with message, bits and repeat, got \"%s\"."
                       " Also all arguments should be positive and bits should be less or equal to 32.\n",
               DEVICE_NAME, kern_buf);
        return -EINVAL;
    }

    printk(KERN_NOTICE "send_message(%u, %u, %u)\n", message, (u8)bits, repeat);
    send_message(message, (u8)bits, repeat);
    return buffer_length;
}
