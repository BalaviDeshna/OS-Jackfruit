#ifndef MONITOR_IOCTL_H
#define MONITOR_IOCTL_H

#include <linux/ioctl.h>

struct container_config {
    pid_t pid;
    unsigned long soft_limit_kb;
    unsigned long hard_limit_kb;
};

#define IOCTL_REGISTER_CONTAINER _IOW('m', 1, struct container_config)
#define IOCTL_UNREGISTER_CONTAINER _IOW('m', 2, pid_t)

#endif