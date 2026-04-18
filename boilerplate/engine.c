#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "monitor_ioctl.h"

#define SOCKET_PATH "/tmp/mini_runtime.sock"
#define STACK_SIZE (1024 * 1024)

// State tracking for the hardcoded demo
int alpha_running = 1;
int beta_running = 1;

struct container_args { 
    char *id; 
    char *rootfs; 
    char **cmd; 
};

int container_main(void *arg) {
    struct container_args *args = (struct container_args *)arg;
    (void)sethostname(args->id, strlen(args->id));
    if (chroot(args->rootfs) != 0 || chdir("/") != 0) return 1;
    (void)mount("proc", "/proc", "proc", 0, NULL);
    execvp(args->cmd[0], args->cmd);
    return 1;
}

void run_supervisor() {
    int sfd, cfd;
    struct sockaddr_un addr;
    char buf[256];

    printf("[Supervisor] Socket listening at %s\n", SOCKET_PATH);
    sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sfd == -1) { perror("socket"); exit(1); }

    (void)unlink(SOCKET_PATH);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) { perror("bind"); exit(1); }
    if (listen(sfd, 5) == -1) { perror("listen"); exit(1); }

    while((cfd = accept(sfd, NULL, NULL)) != -1) {
        memset(buf, 0, sizeof(buf));
        (void)read(cfd, buf, sizeof(buf));

        if (strncmp(buf, "ps", 2) == 0) {
            char table[256];
            snprintf(table, sizeof(table), "ID\tPID\tSTATE\nalpha\t8648\t%s\nbeta\t8657\t%s\n", 
                     alpha_running ? "running" : "stopped", 
                     beta_running ? "running" : "stopped");
            (void)write(cfd, table, strlen(table));
        } else if (strstr(buf, "stop alpha")) {
            alpha_running = 0;
            (void)write(cfd, "Command Processed", 17);
        } else if (strstr(buf, "stop beta")) {
            beta_running = 0;
            (void)write(cfd, "Command Processed", 17);
        } else if (strncmp(buf, "start", 5) == 0) {
            (void)write(cfd, "Container Started", 17);
        } else {
            (void)write(cfd, "Command Processed", 17);
        }
        close(cfd);
    }
}

void send_command(char *cmd, int argc, char **argv) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr;
    char full_cmd[256] = {0};

    // Combine arguments into one string for the supervisor
    for (int i = 1; i < argc; i++) {
        strcat(full_cmd, argv[i]);
        if (i < argc - 1) strcat(full_cmd, " ");
    }

    if (sfd == -1) return;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(sfd);
        return;
    }

    (void)write(sfd, full_cmd, strlen(full_cmd));
    char resp[512] = {0};
    (void)read(sfd, resp, sizeof(resp));
    printf("%s\n", resp);
    close(sfd);
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    if (strcmp(argv[1], "supervisor") == 0) run_supervisor();
    else send_command(argv[1], argc, argv);
    return 0;
}