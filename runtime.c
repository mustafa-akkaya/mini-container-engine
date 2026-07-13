#include "container.h"

    static int pivot_root_syscall(const char *new_root, const char *put_old) {
        return syscall(SYS_pivot_root, new_root, put_old);
    }

    int run_cmd(const char *cmd) {
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Komut basarisiz: %s\n", cmd);
        }
        return ret;
    }

    // ---- CGROUP ----
    static int cgroup_write(const char *path, const char *value) {
        int fd = open(path, O_WRONLY);
        if (fd < 0) return -1;
        if (write(fd, value, strlen(value)) < 0) {
            close(fd);
            return -1;
        }
        close(fd);
        return 0;
    }

    int setup_cgroups(pid_t child_pid, container_config *config) {
        char path[PATH_MAX];
        char value[64];

        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", CGROUP_NAME);
        if (mkdir(path, 0755) < 0 && errno != EEXIST) return -1;

        snprintf(path, sizeof(path), "/sys/fs/cgroup/cgroup.subtree_control");
        cgroup_write(path, "+memory +cpu +pids");

        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cgroup.procs", CGROUP_NAME);
        snprintf(value, sizeof(value), "%d", child_pid);
        if (cgroup_write(path, value) < 0) return -1;
        printf("[CGROUP] Process %d cgroup'a eklendi\n", child_pid);

        if (config->memory_limit > 0) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.max", CGROUP_NAME);
            snprintf(value, sizeof(value), "%ld", config->memory_limit);
            if (cgroup_write(path, value) == 0)
                printf("[CGROUP] Bellek limiti: %ld MB\n", config->memory_limit / (1024 * 1024));
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.swap.max", CGROUP_NAME);
            cgroup_write(path, "0");
        }

        if (config->cpu_percent > 0) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cpu.max", CGROUP_NAME);
            snprintf(value, sizeof(value), "%d 100000", config->cpu_percent * 1000);
            if (cgroup_write(path, value) == 0)
                printf("[CGROUP] CPU limiti: %%%d\n", config->cpu_percent);
        }

        if (config->max_pids > 0) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/pids.max", CGROUP_NAME);
            snprintf(value, sizeof(value), "%d", config->max_pids);
            if (cgroup_write(path, value) == 0)
                printf("[CGROUP] Maks process: %d\n", config->max_pids);
        }

        return 0;
    }

    void cleanup_cgroups(void) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", CGROUP_NAME);
        rmdir(path);
        printf("[CGROUP] Temizlendi\n");
    }

    // ---- DOSYA SISTEMI ----
    int setup_filesystem(const char *rootfs) {
        if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) {
            perror("mount propagation basarisiz");
            return -1;
        }

        if (mount(rootfs, rootfs, NULL, MS_BIND | MS_PRIVATE, NULL) < 0) {
            perror("rootfs bind mount basarisiz");
            return -1;
        }

        char old_root[256];
        snprintf(old_root, sizeof(old_root), "%s/old_root", rootfs);
        mkdir(old_root, 0755);

        if (pivot_root_syscall(rootfs, old_root) < 0) {
            perror("pivot_root basarisiz");
            return -1;
        }

        if (chdir("/") < 0) return -1;

        mkdir("/proc", 0755);
        mount("proc", "/proc", "proc", 0, NULL);

        mkdir("/sys", 0755);
        mount("sysfs", "/sys", "sysfs", 0, NULL);

        if (umount2("/old_root", MNT_DETACH) < 0) {
            perror("old_root unmount basarisiz");
            return -1;
        }
        rmdir("/old_root");

        printf("[CONTAINER] Dosya sistemi izole edildi\n");
        return 0;
    }

    // ---- AG (HOST) ----
    int setup_network_host(pid_t child_pid) {
        char cmd[512];
        printf("[AG] Ag altyapisi kuruluyor...\n");

        snprintf(cmd, sizeof(cmd), "ip link add %s type bridge 2>/dev/null", BRIDGE_NAME);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s 2>/dev/null", BRIDGE_IP, BRIDGE_NAME);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip link set %s up", BRIDGE_NAME);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd), "ip link add %s type veth peer name %s", VETH_HOST, VETH_CONTAINER);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip link set %s master %s", VETH_HOST, BRIDGE_NAME);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip link set %s up", VETH_HOST);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd), "ip link set %s netns %d", VETH_CONTAINER, child_pid);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd),
                 "iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -o %s -j MASQUERADE 2>/dev/null",
                 HOST_INTERFACE);
        run_cmd(cmd);
        run_cmd("echo 1 > /proc/sys/net/ipv4/ip_forward");

        printf("[AG] Ag altyapisi hazir\n");
        return 0;
    }

    // ---- AG (CONTAINER) ----
    int setup_network_container(void) {
        char cmd[512];

        run_cmd("ip link set lo up");
        snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s", CONTAINER_IP, VETH_CONTAINER);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip link set %s up", VETH_CONTAINER);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip route add default via %s", BRIDGE_IP_BARE);
        run_cmd(cmd);

        printf("[AG] Container agi yapilandirildi: %s\n", CONTAINER_IP);
        return 0;
    }

    void cleanup_network(void) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "ip link del %s 2>/dev/null", VETH_HOST);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd), "ip link del %s 2>/dev/null", BRIDGE_NAME);
        run_cmd(cmd);
        snprintf(cmd, sizeof(cmd),
                 "iptables -t nat -D POSTROUTING -s 10.0.0.0/24 -o %s -j MASQUERADE 2>/dev/null",
                 HOST_INTERFACE);
        run_cmd(cmd);
        printf("[AG] Ag temizlendi\n");
    }

    // ---- CONTAINER MAIN ----
    int container_main(void *arg) {
        container_config *config = (container_config *)arg;

        printf("[CONTAINER] PID: %d\n", getpid());

        if (sethostname(config->hostname, strlen(config->hostname)) < 0) {
            perror("sethostname basarisiz");
            return 1;
        }
        printf("[CONTAINER] Hostname: %s\n", config->hostname);

        if (setup_filesystem(config->rootfs) < 0) return 1;

        if (config->enable_net) {
            FILE *f = fopen("/etc/resolv.conf", "w");
            if (f) { fprintf(f, "nameserver 8.8.8.8\n"); fclose(f); }
            usleep(500000);
            setup_network_container();
        }

        printf("[CONTAINER] Komut calistiriliyor: %s\n", config->argv[0]);
        printf("==========================================\n");

        execvp(config->argv[0], config->argv);
        perror("execvp basarisiz");
        return 1;
    }
