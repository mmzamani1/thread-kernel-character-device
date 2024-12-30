#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>

#define QUEUE_SIZE 30
#define DEVICE_NAME "myQueue"

static int major_number;
static char queue[QUEUE_SIZE];
static int front = 0, rear = 0, count = 0;

static DEFINE_MUTEX(queue_lock);

#define IOCTL_GET_COUNT _IOR('q', 1, int) // Define an IOCTL command

static int device_open(struct inode *inode, struct file *file) {
    return 0;
}

static ssize_t device_write(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    char ch;

    if (len == 0) return 0;

    mutex_lock(&queue_lock);

    if (count == QUEUE_SIZE) {
        mutex_unlock(&queue_lock);
        printk(KERN_INFO "Queue is full.\n");
        return -ENOSPC;
    }

    if (copy_from_user(&ch, buffer, 1)) {
        mutex_unlock(&queue_lock);
        return -EFAULT;
    }

    queue[rear] = ch;
    rear = (rear + 1) % QUEUE_SIZE;
    count++;

    printk(KERN_INFO "Added '%c' to the queue.\ncount : %i\n", ch, count);

    mutex_unlock(&queue_lock);

    return 1;
}

static ssize_t device_read(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    char ch;

    mutex_lock(&queue_lock);

    if (count == 0) {
        mutex_unlock(&queue_lock);
        printk(KERN_INFO "Queue is empty.\n");
        return 0;
    }

    ch = queue[front];
    front = (front + 1) % QUEUE_SIZE;
    count--;

    if (copy_to_user(buffer, &ch, 1)) {
        mutex_unlock(&queue_lock);
        return -EFAULT;
    }

    printk(KERN_INFO "Read '%c' from the queue.\n", ch);

    mutex_unlock(&queue_lock);

    return 1;
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    switch (cmd) {
        case IOCTL_GET_COUNT:
            if (copy_to_user((int __user *)arg, &count, sizeof(count))) {
                return -EFAULT;
            }
            break;

        default:
            return -EINVAL;
    }

    return 0;
}

static int device_release(struct inode *inode, struct file *file) {
    return 0;
}

static struct file_operations fops = {
    .open = device_open,
    .write = device_write,
    .read = device_read,
    .unlocked_ioctl = device_ioctl, // Correct signature
    .release = device_release,
};

static int __init my_module_init(void) {
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "Failed to register device.\n");
        return major_number;
    }

    printk(KERN_INFO "Device registered with major number %d.\n", major_number);
    return 0;
}

static void __exit my_module_exit(void) {
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "Device unregistered.\n");
}

module_init(my_module_init);
module_exit(my_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("mmzamani");
MODULE_DESCRIPTION("Kernel module for character queue");
