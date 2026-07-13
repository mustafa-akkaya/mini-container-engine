#ifndef CONTAINER_H
    #define CONTAINER_H

    #define _GNU_SOURCE
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include <unistd.h>
    #include <sched.h>
    #include <sys/wait.h>
    #include <sys/mount.h>
    #include <sys/stat.h>
    #include <sys/syscall.h>
    #include <errno.h>
    #include <fcntl.h>
    #include <limits.h>
    #include <time.h>
    #include <dirent.h>
    #include <signal.h>

    #define STACK_SIZE (1024 * 1024)
    #define CGROUP_NAME "mini-container"

    #define VETH_HOST "veth0"
    #define VETH_CONTAINER "veth1"
    #define BRIDGE_NAME "br-container"
    #define CONTAINER_IP "10.0.0.2/24"
    #define BRIDGE_IP "10.0.0.1/24"
    #define BRIDGE_IP_BARE "10.0.0.1"
    #define HOST_INTERFACE "enp0s3"

    #define CONTAINER_STATE_DIR "/tmp/mini-container"
    #define IMAGES_DIR "images"

    typedef struct {
        int argc;
        char **argv;
        char *rootfs;
        char *hostname;
        char *image;
        long memory_limit;
        int cpu_percent;
        int max_pids;
        int enable_net;
    } container_config;

    // runtime.c fonksiyonlari
    int run_cmd(const char *cmd);
    int setup_cgroups(pid_t child_pid, container_config *config);
    void cleanup_cgroups(void);
    int setup_filesystem(const char *rootfs);
    int setup_network_host(pid_t child_pid);
    int setup_network_container(void);
    void cleanup_network(void);
    int container_main(void *arg);

    #endif
