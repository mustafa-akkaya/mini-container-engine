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

    #define STACK_SIZE (1024 * 1024)
    #define CGROUP_NAME "mini-container"

    // Ag ayarlari
    #define VETH_HOST "veth0"
    #define VETH_CONTAINER "veth1"
    #define BRIDGE_NAME "br-container"
    #define CONTAINER_IP "10.0.0.2/24"
    #define BRIDGE_IP "10.0.0.1/24"
    #define BRIDGE_IP_BARE "10.0.0.1"
    #define HOST_INTERFACE "enp0s3"

    typedef struct {
        int argc;
        char **argv;
        char *rootfs;
        long memory_limit;
        int cpu_percent;
        int max_pids;
        int enable_net;
    } container_config;

    static int pivot_root(const char *new_root, const char *put_old) {
        return syscall(SYS_pivot_root, new_root, put_old);
    }

    // ---- YARDIMCI: Komut calistir ve sonucu kontrol et ----
    static int run_cmd(const char *cmd) {
        int ret = system(cmd);
        if (ret != 0) {
            fprintf(stderr, "Komut basarisiz: %s\n", cmd);
        }
        return ret;
    }

    // ---- CGROUP FONKSIYONLARI ----
    static int cgroup_write(const char *path, const char *value) {
        int fd = open(path, O_WRONLY);
        if (fd < 0) {
            perror(path);
            return -1;
        }
        if (write(fd, value, strlen(value)) < 0) {
            perror(path);
            close(fd);
            return -1;
        }
        close(fd);
        return 0;
    }

    static int setup_cgroups(pid_t child_pid, container_config *config) {
        char path[PATH_MAX];
        char value[64];

        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", CGROUP_NAME);
        if (mkdir(path, 0755) < 0 && errno != EEXIST) {
            perror("cgroup dizini olusturulamadi");
            return -1;
        }

        snprintf(path, sizeof(path), "/sys/fs/cgroup/cgroup.subtree_control");
        cgroup_write(path, "+memory +cpu +pids");

        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cgroup.procs", CGROUP_NAME);
        snprintf(value, sizeof(value), "%d", child_pid);
        if (cgroup_write(path, value) < 0) {
            fprintf(stderr, "Process cgroup'a eklenemedi\n");
            return -1;
        }
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

    static void cleanup_cgroups(void) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", CGROUP_NAME);
        rmdir(path);
        printf("[CGROUP] Temizlendi\n");
    }

    // ---- AG IZOLASYONU (HOST TARAFI) ----
    // Container baslamadan once host tarafinda ag altyapisini kurar
    static int setup_network_host(pid_t child_pid) {
        char cmd[512];

        printf("[AG] Ag altyapisi kuruluyor...\n");

        // 1. Bridge olustur (container'larin bagli oldugu sanal switch)
        snprintf(cmd, sizeof(cmd), "ip link add %s type bridge 2>/dev/null", BRIDGE_NAME);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s 2>/dev/null", BRIDGE_IP, BRIDGE_NAME);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd), "ip link set %s up", BRIDGE_NAME);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "Bridge aktiflestiremedi\n");
            return -1;
        }
        printf("[AG] Bridge %s olusturuldu (%s)\n", BRIDGE_NAME, BRIDGE_IP);

        // 2. veth cifti olustur (sanal ethernet kablosu)
        snprintf(cmd, sizeof(cmd), "ip link add %s type veth peer name %s",
                 VETH_HOST, VETH_CONTAINER);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "veth cifti olusturulamadi\n");
            return -1;
        }
        printf("[AG] veth cifti olusturuldu: %s <-> %s\n", VETH_HOST, VETH_CONTAINER);

        // 3. Host tarafini bridge'e bagla
        snprintf(cmd, sizeof(cmd), "ip link set %s master %s", VETH_HOST, BRIDGE_NAME);
        run_cmd(cmd);

        snprintf(cmd, sizeof(cmd), "ip link set %s up", VETH_HOST);
        run_cmd(cmd);

        // 4. Container tarafini container'in network namespace'ine tasi
        snprintf(cmd, sizeof(cmd), "ip link set %s netns %d", VETH_CONTAINER, child_pid);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "veth container namespace'ine tasinamadi\n");
            return -1;
        }
        printf("[AG] %s container namespace'ine tasindi\n", VETH_CONTAINER);

        // 5. NAT kur (container internete cikabilsin)
        snprintf(cmd, sizeof(cmd),
                 "iptables -t nat -A POSTROUTING -s 10.0.0.0/24 -o %s -j MASQUERADE 2>/dev/null",
                 HOST_INTERFACE);
        run_cmd(cmd);

        // IP forwarding aktifle
        run_cmd("echo 1 > /proc/sys/net/ipv4/ip_forward");

        printf("[AG] NAT ve IP forwarding aktif\n");
        return 0;
    }

    // ---- AG IZOLASYONU (CONTAINER TARAFI) ----
    // Container icinden calisir, kendi ag arayuzunu yapilandirir
    static int setup_network_container(void) {
        char cmd[512];

        // 1. Loopback arayuzunu aktifle
        run_cmd("ip link set lo up");

        // 2. veth arayuzune IP ata ve aktifle
        snprintf(cmd, sizeof(cmd), "ip addr add %s dev %s", CONTAINER_IP, VETH_CONTAINER);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "Container IP atanamadi\n");
            return -1;
        }

        snprintf(cmd, sizeof(cmd), "ip link set %s up", VETH_CONTAINER);
        run_cmd(cmd);

        // 3. Default gateway ayarla (bridge IP'si)
        snprintf(cmd, sizeof(cmd), "ip route add default via %s", BRIDGE_IP_BARE);
        run_cmd(cmd);

        printf("[AG] Container agi yapilandirildi: %s\n", CONTAINER_IP);
        return 0;
    }

    // ---- AG TEMIZLIGI ----
    static void cleanup_network(void) {
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

    // ---- DOSYA SISTEMI IZOLASYONU ----
    static int setup_filesystem(const char *rootfs) {
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

        if (pivot_root(rootfs, old_root) < 0) {
            perror("pivot_root basarisiz");
            return -1;
        }

        if (chdir("/") < 0) {
            perror("chdir basarisiz");
            return -1;
        }

        mkdir("/proc", 0755);
        if (mount("proc", "/proc", "proc", 0, NULL) < 0) {
            perror("proc mount basarisiz");
            return -1;
        }

        // /sys mount et (ip komutu icin gerekli)
        mkdir("/sys", 0755);
        if (mount("sysfs", "/sys", "sysfs", 0, NULL) < 0) {
            perror("sysfs mount basarisiz");
        }

        if (umount2("/old_root", MNT_DETACH) < 0) {
            perror("old_root unmount basarisiz");
            return -1;
        }
        rmdir("/old_root");

        printf("[CONTAINER] Dosya sistemi izole edildi\n");
        return 0;
    }

    // ---- CONTAINER ANA FONKSIYONU ----
    static int container_main(void *arg) {
        container_config *config = (container_config *)arg;

        printf("[CONTAINER] PID: %d\n", getpid());

        if (sethostname("mini-container", 14) < 0) {
            perror("sethostname basarisiz");
            return 1;
        }
        printf("[CONTAINER] Hostname: mini-container\n");

        if (setup_filesystem(config->rootfs) < 0) {
            return 1;
        }

        // Ag yapilandirmasi (container tarafi)
        if (config->enable_net) {
            // DNS ayarla
            FILE *f = fopen("/etc/resolv.conf", "w");
            if (f) {
                fprintf(f, "nameserver 8.8.8.8\n");
                fclose(f);
            }

            // Ag arayuzlerinin hazir olmasini bekle
            usleep(500000);
            setup_network_container();
        }

        printf("[CONTAINER] Komut calistiriliyor: %s\n", config->argv[0]);
        printf("==========================================\n");

        execvp(config->argv[0], config->argv);

        perror("execvp basarisiz");
        return 1;
    }

    // ---- KULLANIM BILGISI ----
    static void print_usage(void) {
        fprintf(stderr, "Kullanim: sudo ./container run [secenekler] <komut>\n");
        fprintf(stderr, "\nSecenekler:\n");
        fprintf(stderr, "  --memory <MB>     Bellek limiti (MB cinsinden)\n");
        fprintf(stderr, "  --cpu <yuzde>     CPU limiti (yuzde cinsinden)\n");
        fprintf(stderr, "  --pids <sayi>     Maksimum process sayisi\n");
        fprintf(stderr, "  --net             Ag izolasyonunu aktifle\n");
        fprintf(stderr, "\nOrnekler:\n");
        fprintf(stderr, "  sudo ./container run /bin/sh\n");
        fprintf(stderr, "  sudo ./container run --net /bin/sh\n");
        fprintf(stderr, "  sudo ./container run --memory 64 --cpu 50 --pids 10 --net /bin/sh\n");
    }

    // ---- ANA PROGRAM ----
    int main(int argc, char *argv[]) {
        if (argc < 3) {
            print_usage();
            return 1;
        }

        if (strcmp(argv[1], "run") != 0) {
            fprintf(stderr, "Bilinmeyen komut: %s\n", argv[1]);
            print_usage();
            return 1;
        }

        printf("=== Mini Container Runtime ===\n");
        printf("[ANA PROCESS] PID: %d\n", getpid());

        container_config config;
        config.rootfs = "./rootfs";
        config.memory_limit = 0;
        config.cpu_percent = 0;
        config.max_pids = 0;
        config.enable_net = 0;

        int cmd_start = 2;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--memory") == 0 && i + 1 < argc) {
                config.memory_limit = atol(argv[++i]) * 1024 * 1024;
                cmd_start = i + 1;
            } else if (strcmp(argv[i], "--cpu") == 0 && i + 1 < argc) {
                config.cpu_percent = atoi(argv[++i]);
                cmd_start = i + 1;
            } else if (strcmp(argv[i], "--pids") == 0 && i + 1 < argc) {
                config.max_pids = atoi(argv[++i]);
                cmd_start = i + 1;
            } else if (strcmp(argv[i], "--net") == 0) {
                config.enable_net = 1;
                cmd_start = i + 1;
            } else {
                break;
            }
        }

        if (cmd_start >= argc) {
            fprintf(stderr, "Hata: Komut belirtilmedi\n");
            print_usage();
            return 1;
        }

        config.argc = argc - cmd_start;
        config.argv = &argv[cmd_start];

        char *stack = malloc(STACK_SIZE);
        if (!stack) {
            perror("Stack ayirma basarisiz");
            return 1;
        }

        int clone_flags = CLONE_NEWUTS
                        | CLONE_NEWPID
                        | CLONE_NEWNS
                        | SIGCHLD;

        // Ag izolasyonu isteniyorsa CLONE_NEWNET ekle
        if (config.enable_net) {
            clone_flags |= CLONE_NEWNET;
        }

        pid_t child_pid = clone(container_main, stack + STACK_SIZE, clone_flags, &config);

        if (child_pid < 0) {
            perror("clone basarisiz");
            free(stack);
            return 1;
        }

        printf("[ANA PROCESS] Container PID: %d\n", child_pid);

        // Cgroups
        if (config.memory_limit > 0 || config.cpu_percent > 0 || config.max_pids > 0) {
            setup_cgroups(child_pid, &config);
        }

        // Ag (host tarafi)
        if (config.enable_net) {
            setup_network_host(child_pid);
        }

        int status;
        waitpid(child_pid, &status, 0);

        printf("==========================================\n");
        printf("[ANA PROCESS] Container sonlandi (cikis kodu: %d)\n",
               WEXITSTATUS(status));

        if (config.enable_net) {
            cleanup_network();
        }
        cleanup_cgroups();
        free(stack);
        return 0;
    }
