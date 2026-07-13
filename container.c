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

    typedef struct {
        int argc;
        char **argv;
        char *rootfs;
        long memory_limit;
        int cpu_percent;
        int max_pids;
    } container_config;

    static int pivot_root(const char *new_root, const char *put_old) {
        return syscall(SYS_pivot_root, new_root, put_old);
    }

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
            if (cgroup_write(path, value) == 0) {
                printf("[CGROUP] Bellek limiti: %ld MB\n", config->memory_limit / (1024 * 1024));
            }
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.swap.max", CGROUP_NAME);
            cgroup_write(path, "0");
        }

        if (config->cpu_percent > 0) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cpu.max", CGROUP_NAME);
            snprintf(value, sizeof(value), "%d 100000", config->cpu_percent * 1000);
            if (cgroup_write(path, value) == 0) {
                printf("[CGROUP] CPU limiti: %%%d\n", config->cpu_percent);
            }
        }

        if (config->max_pids > 0) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/pids.max", CGROUP_NAME);
            snprintf(value, sizeof(value), "%d", config->max_pids);
            if (cgroup_write(path, value) == 0) {
                printf("[CGROUP] Maks process: %d\n", config->max_pids);
            }
        }

        return 0;
    }

    static void cleanup_cgroups(void) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "/sys/fs/cgroup/%s", CGROUP_NAME);
        rmdir(path);
        printf("[CGROUP] Temizlendi\n");
    }

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

        if (umount2("/old_root", MNT_DETACH) < 0) {
            perror("old_root unmount basarisiz");
            return -1;
        }
        rmdir("/old_root");

        printf("[CONTAINER] Dosya sistemi izole edildi\n");
        return 0;
    }

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

        printf("[CONTAINER] Komut calistiriliyor: %s\n", config->argv[0]);
        printf("==========================================\n");

        execvp(config->argv[0], config->argv);

        perror("execvp basarisiz");
        return 1;
    }

    static void print_usage(void) {
        fprintf(stderr, "Kullanim: sudo ./container run [secenekler] <komut>\n");
        fprintf(stderr, "\nSecenekler:\n");
        fprintf(stderr, "  --memory <MB>     Bellek limiti (MB cinsinden)\n");
        fprintf(stderr, "  --cpu <yuzde>     CPU limiti (yuzde cinsinden)\n");
        fprintf(stderr, "  --pids <sayi>     Maksimum process sayisi\n");
        fprintf(stderr, "\nOrnekler:\n");
        fprintf(stderr, "  sudo ./container run /bin/sh\n");
        fprintf(stderr, "  sudo ./container run --memory 64 --cpu 50 --pids 10 /bin/sh\n");
    }

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

        pid_t child_pid = clone(container_main, stack + STACK_SIZE, clone_flags, &config);

        if (child_pid < 0) {
            perror("clone basarisiz");
            free(stack);
            return 1;
        }

        printf("[ANA PROCESS] Container PID: %d\n", child_pid);

        if (config.memory_limit > 0 || config.cpu_percent > 0 || config.max_pids > 0) {
            setup_cgroups(child_pid, &config);
        }

        int status;
        waitpid(child_pid, &status, 0);

        printf("==========================================\n");
        printf("[ANA PROCESS] Container sonlandi (cikis kodu: %d)\n",
               WEXITSTATUS(status));

        cleanup_cgroups();
        free(stack);
        return 0;
    }
