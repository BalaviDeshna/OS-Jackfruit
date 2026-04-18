#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/timer.h>
#include "monitor_ioctl.h"

#define DEVICE_NAME "container_monitor"

struct monitored_proc {
    struct container_config config;
    struct list_head list;
};

static LIST_HEAD(proc_list);
static DEFINE_MUTEX(list_lock);
static struct timer_list monitor_timer;

static void check_memory_usage(struct timer_list *t) {
    struct monitored_proc *entry, *tmp;
    struct task_struct *task;
    unsigned long rss;

    mutex_lock(&list_lock);
    list_for_each_entry_safe(entry, tmp, &proc_list, list) {
        task = pid_task(find_vpid(entry->config.pid), PIDTYPE_PID);
        if (!task) {
            list_del(&entry->list);
            kfree(entry);
            continue;
        }

        if (task->mm) {
            rss = get_mm_rss(task->mm) << (PAGE_SHIFT - 10); // KB
            if (rss > entry->config.hard_limit_kb) {
                printk(KERN_ALERT "Monitor: Hard limit hit for PID %d. Killing.\n", entry->config.pid);
                send_sig(SIGKILL, task, 0);
            } else if (rss > entry->config.soft_limit_kb) {
                printk(KERN_WARNING "Monitor: Soft limit warning for PID %d.\n", entry->config.pid);
            }
        }
    }
    mutex_unlock(&list_lock);
    mod_timer(&monitor_timer, jiffies + msecs_to_jiffies(1000));
}

static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct container_config conf;
    struct monitored_proc *new_proc;

    switch (cmd) {
        case IOCTL_REGISTER_CONTAINER:
            if (copy_from_user(&conf, (struct container_config __user *)arg, sizeof(conf)))
                return -EFAULT;
            new_proc = kmalloc(sizeof(*new_proc), GFP_KERNEL);
            new_proc->config = conf;
            mutex_lock(&list_lock);
            list_add(&new_proc->list, &proc_list);
            mutex_unlock(&list_lock);
            printk(KERN_INFO "Monitor: Registered PID %d\n", conf.pid);
            break;
    }
    return 0;
}

static struct file_operations fops = { .unlocked_ioctl = device_ioctl };

static int __init monitor_init(void) {
    register_chrdev(240, DEVICE_NAME, &fops);
    timer_setup(&monitor_timer, check_memory_usage, 0);
    mod_timer(&monitor_timer, jiffies + msecs_to_jiffies(1000));
    return 0;
}

static void __exit monitor_exit(void) {
    del_timer(&monitor_timer);
    unregister_chrdev(240, DEVICE_NAME);
}

module_init(monitor_init);
module_exit(monitor_exit);
MODULE_LICENSE("GPL");