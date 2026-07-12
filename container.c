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

    #define STACK_SIZE (1024 * 1024)

    typedef struct {
        int argc;
        char **argv;
        char *rootfs;
    } container_config;

    static int pivot_root(const char *new_root, const char *put_old) {
        return syscall(SYS_pivot_root, new_root, put_old);
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

    int main(int argc, char *argv[]) {
        if (argc < 3) {
            fprintf(stderr, "Kullanim: sudo ./container run <komut> [arguman...]\n");
            fprintf(stderr, "Ornek:    sudo ./container run /bin/sh\n");
            return 1;
        }

        if (strcmp(argv[1], "run") != 0) {
            fprintf(stderr, "Bilinmeyen komut: %s\n", argv[1]);
            fprintf(stderr, "Kullanim: sudo ./container run <komut>\n");
            return 1;
        }

        printf("=== Mini Container Runtime ===\n");
        printf("[ANA PROCESS] PID: %d\n", getpid());

        container_config config;
        config.argc = argc - 2;
        config.argv = &argv[2];
        config.rootfs = "./rootfs";

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

        int status;
        waitpid(child_pid, &status, 0);

        printf("==========================================\n");
        printf("[ANA PROCESS] Container sonlandi (cikis kodu: %d)\n",
               WEXITSTATUS(status));

        free(stack);
        return 0;
    }
