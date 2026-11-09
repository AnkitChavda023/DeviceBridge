#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "ioctl_commands.h"

#define DEVICE_NAME "vdev"
#define CLASS_NAME "vdev_class"
#define COPY_CHUNK 128

struct vdev_state {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    struct mutex lock;
    wait_queue_head_t read_wq;
    wait_queue_head_t write_wq;

    char buffer[VDEV_BUFFER_SIZE];
    unsigned int head;
    unsigned int tail;
    unsigned int length;

    unsigned int interval_ms;
    unsigned int sequence;
    bool shutting_down;
    struct delayed_work sample_work;
    struct vdev_stats stats;
};

static struct vdev_state vdev;

static unsigned int ring_space(void)
{
    return VDEV_BUFFER_SIZE - vdev.length;
}

static void ring_clear(void)
{
    vdev.head = 0;
    vdev.tail = 0;
    vdev.length = 0;
}

static void ring_push(const char *data, unsigned int count)
{
    unsigned int first = min_t(unsigned int, count, VDEV_BUFFER_SIZE - vdev.head);

    memcpy(vdev.buffer + vdev.head, data, first);
    memcpy(vdev.buffer, data + first, count - first);
    vdev.head = (vdev.head + count) % VDEV_BUFFER_SIZE;
    vdev.length += count;
}

static void sample_work_fn(struct work_struct *work)
{
    char line[64];
    int len;

    mutex_lock(&vdev.lock);
    if (vdev.interval_ms == 0 || vdev.shutting_down) {
        mutex_unlock(&vdev.lock);
        return;
    }

    vdev.sequence++;
    len = scnprintf(line, sizeof(line), "SAMPLE seq=%u value=%d\n", vdev.sequence,
                    20000 + (int)(vdev.sequence % 100) * 50);

    if (ring_space() >= (unsigned int)len) {
        ring_push(line, len);
        vdev.stats.samples_generated++;
        wake_up_interruptible(&vdev.read_wq);
    } else {
        vdev.stats.samples_dropped++;
    }

    schedule_delayed_work(&vdev.sample_work, msecs_to_jiffies(vdev.interval_ms));
    mutex_unlock(&vdev.lock);
}

static int vdev_open(struct inode *inode, struct file *file)
{
    nonseekable_open(inode, file);

    mutex_lock(&vdev.lock);
    vdev.stats.opens++;
    mutex_unlock(&vdev.lock);
    return 0;
}

static int vdev_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t vdev_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    size_t n;
    size_t first;

    if (count == 0)
        return 0;

    if (mutex_lock_interruptible(&vdev.lock))
        return -ERESTARTSYS;

    while (vdev.length == 0) {
        mutex_unlock(&vdev.lock);

        if (file->f_flags & O_NONBLOCK)
            return -EAGAIN;

        if (wait_event_interruptible(vdev.read_wq,
                                     READ_ONCE(vdev.length) > 0 || READ_ONCE(vdev.shutting_down)))
            return -ERESTARTSYS;

        if (READ_ONCE(vdev.shutting_down))
            return -ENODEV;

        if (mutex_lock_interruptible(&vdev.lock))
            return -ERESTARTSYS;
    }

    n = min_t(size_t, count, vdev.length);
    first = min_t(size_t, n, VDEV_BUFFER_SIZE - vdev.tail);

    if (copy_to_user(buf, vdev.buffer + vdev.tail, first)) {
        mutex_unlock(&vdev.lock);
        return -EFAULT;
    }
    if (n > first && copy_to_user(buf + first, vdev.buffer, n - first)) {
        mutex_unlock(&vdev.lock);
        return -EFAULT;
    }

    vdev.tail = (vdev.tail + n) % VDEV_BUFFER_SIZE;
    vdev.length -= n;
    vdev.stats.bytes_read += n;
    mutex_unlock(&vdev.lock);

    wake_up_interruptible(&vdev.write_wq);
    return n;
}

static ssize_t vdev_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    char chunk[COPY_CHUNK];
    size_t done = 0;
    size_t want;

    if (count == 0)
        return 0;

    while (done < count) {
        if (mutex_lock_interruptible(&vdev.lock))
            return done ? done : -ERESTARTSYS;

        while (ring_space() == 0) {
            mutex_unlock(&vdev.lock);

            if (file->f_flags & O_NONBLOCK)
                return done ? done : -EAGAIN;

            if (wait_event_interruptible(vdev.write_wq,
                                         READ_ONCE(vdev.length) < VDEV_BUFFER_SIZE ||
                                             READ_ONCE(vdev.shutting_down)))
                return done ? done : -ERESTARTSYS;

            if (READ_ONCE(vdev.shutting_down))
                return done ? done : -ENODEV;

            if (mutex_lock_interruptible(&vdev.lock))
                return done ? done : -ERESTARTSYS;
        }

        want = min_t(size_t, count - done, sizeof(chunk));
        want = min_t(size_t, want, ring_space());

        if (copy_from_user(chunk, buf + done, want)) {
            mutex_unlock(&vdev.lock);
            return done ? done : -EFAULT;
        }

        ring_push(chunk, want);
        vdev.stats.bytes_written += want;
        mutex_unlock(&vdev.lock);

        wake_up_interruptible(&vdev.read_wq);
        done += want;
    }

    return done;
}

static __poll_t vdev_poll(struct file *file, poll_table *wait)
{
    __poll_t mask = 0;

    poll_wait(file, &vdev.read_wq, wait);
    poll_wait(file, &vdev.write_wq, wait);

    mutex_lock(&vdev.lock);
    if (vdev.length > 0)
        mask |= EPOLLIN | EPOLLRDNORM;
    if (vdev.length < VDEV_BUFFER_SIZE)
        mask |= EPOLLOUT | EPOLLWRNORM;
    if (vdev.shutting_down)
        mask |= EPOLLHUP;
    mutex_unlock(&vdev.lock);

    return mask;
}

static long vdev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    void __user *argp = (void __user *)arg;

    switch (cmd) {
    case VDEV_GET_DEVICE_INFO: {
        struct vdev_info info;

        memset(&info, 0, sizeof(info));
        strscpy(info.name, DEVICE_NAME, sizeof(info.name));
        info.version = VDEV_VERSION;
        info.buffer_size = VDEV_BUFFER_SIZE;
        if (copy_to_user(argp, &info, sizeof(info)))
            return -EFAULT;
        return 0;
    }

    case VDEV_GET_STATUS: {
        struct vdev_status status;

        memset(&status, 0, sizeof(status));
        mutex_lock(&vdev.lock);
        status.buffered_bytes = vdev.length;
        status.sample_interval_ms = vdev.interval_ms;
        status.sequence = vdev.sequence;
        status.generating = vdev.interval_ms > 0;
        mutex_unlock(&vdev.lock);
        if (copy_to_user(argp, &status, sizeof(status)))
            return -EFAULT;
        return 0;
    }

    case VDEV_SET_SAMPLE_INTERVAL: {
        __u32 interval;

        if (!(file->f_mode & FMODE_WRITE))
            return -EPERM;
        if (copy_from_user(&interval, argp, sizeof(interval)))
            return -EFAULT;
        if (interval != 0 && (interval < VDEV_MIN_INTERVAL_MS || interval > VDEV_MAX_INTERVAL_MS))
            return -EINVAL;

        mutex_lock(&vdev.lock);
        vdev.interval_ms = interval;
        mutex_unlock(&vdev.lock);

        if (interval > 0)
            mod_delayed_work(system_wq, &vdev.sample_work, msecs_to_jiffies(interval));
        else
            cancel_delayed_work(&vdev.sample_work);
        return 0;
    }

    case VDEV_RESET_DEVICE:
        if (!(file->f_mode & FMODE_WRITE))
            return -EPERM;
        mutex_lock(&vdev.lock);
        ring_clear();
        vdev.interval_ms = 0;
        vdev.sequence = 0;
        vdev.stats.bytes_read = 0;
        vdev.stats.bytes_written = 0;
        vdev.stats.samples_generated = 0;
        vdev.stats.samples_dropped = 0;
        mutex_unlock(&vdev.lock);
        cancel_delayed_work(&vdev.sample_work);
        wake_up_interruptible(&vdev.write_wq);
        return 0;

    case VDEV_CLEAR_BUFFER:
        if (!(file->f_mode & FMODE_WRITE))
            return -EPERM;
        mutex_lock(&vdev.lock);
        ring_clear();
        mutex_unlock(&vdev.lock);
        wake_up_interruptible(&vdev.write_wq);
        return 0;

    case VDEV_GET_STATS: {
        struct vdev_stats stats;

        mutex_lock(&vdev.lock);
        stats = vdev.stats;
        mutex_unlock(&vdev.lock);
        if (copy_to_user(argp, &stats, sizeof(stats)))
            return -EFAULT;
        return 0;
    }

    default:
        return -ENOTTY;
    }
}

static const struct file_operations vdev_fops = {
    .owner = THIS_MODULE,
    .open = vdev_open,
    .release = vdev_release,
    .read = vdev_read,
    .write = vdev_write,
    .poll = vdev_poll,
    .unlocked_ioctl = vdev_ioctl,
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
    .llseek = no_llseek,
#endif
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 2, 0)
static char *vdev_devnode(const struct device *dev, umode_t *mode)
#else
static char *vdev_devnode(struct device *dev, umode_t *mode)
#endif
{
    if (mode)
        *mode = 0666;
    return NULL;
}

static int __init vdev_init(void)
{
    int ret;

    mutex_init(&vdev.lock);
    init_waitqueue_head(&vdev.read_wq);
    init_waitqueue_head(&vdev.write_wq);
    INIT_DELAYED_WORK(&vdev.sample_work, sample_work_fn);
    ring_clear();

    ret = alloc_chrdev_region(&vdev.devno, 0, 1, DEVICE_NAME);
    if (ret)
        return ret;

    cdev_init(&vdev.cdev, &vdev_fops);
    vdev.cdev.owner = THIS_MODULE;
    ret = cdev_add(&vdev.cdev, vdev.devno, 1);
    if (ret)
        goto err_region;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    vdev.class = class_create(CLASS_NAME);
#else
    vdev.class = class_create(THIS_MODULE, CLASS_NAME);
#endif
    if (IS_ERR(vdev.class)) {
        ret = PTR_ERR(vdev.class);
        goto err_cdev;
    }
    vdev.class->devnode = vdev_devnode;

    vdev.device = device_create(vdev.class, NULL, vdev.devno, NULL, DEVICE_NAME "0");
    if (IS_ERR(vdev.device)) {
        ret = PTR_ERR(vdev.device);
        goto err_class;
    }

    pr_info("vdev: loaded, /dev/%s0 major %d\n", DEVICE_NAME, MAJOR(vdev.devno));
    return 0;

err_class:
    class_destroy(vdev.class);
err_cdev:
    cdev_del(&vdev.cdev);
err_region:
    unregister_chrdev_region(vdev.devno, 1);
    return ret;
}

static void __exit vdev_exit(void)
{
    mutex_lock(&vdev.lock);
    vdev.shutting_down = true;
    vdev.interval_ms = 0;
    mutex_unlock(&vdev.lock);

    wake_up_interruptible(&vdev.read_wq);
    wake_up_interruptible(&vdev.write_wq);
    cancel_delayed_work_sync(&vdev.sample_work);

    device_destroy(vdev.class, vdev.devno);
    class_destroy(vdev.class);
    cdev_del(&vdev.cdev);
    unregister_chrdev_region(vdev.devno, 1);
    pr_info("vdev: unloaded\n");
}

module_init(vdev_init);
module_exit(vdev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Device Framework Project");
MODULE_DESCRIPTION("Virtual character device with a bounded buffer and a sample generator");
MODULE_VERSION("1.0");
