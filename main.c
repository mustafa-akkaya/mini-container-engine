#include "container.h"

    // ---- CONTAINER ID OLUSTUR ----
    static void generate_id(char *id, size_t len) {
        const char chars[] = "abcdef0123456789";
        srand(time(NULL) ^ getpid());
        for (size_t i = 0; i < len - 1; i++) {
            id[i] = chars[rand() % (sizeof(chars) - 1)];
        }
        id[len - 1] = '\0';
    }

    // ---- CONTAINER DURUMUNU KAYDET ----
    static void save_container_state(const char *id, pid_t pid, container_config *config) {
        char path[PATH_MAX];
        mkdir(CONTAINER_STATE_DIR, 0755);

        snprintf(path, sizeof(path), "%s/%s", CONTAINER_STATE_DIR, id);
        mkdir(path, 0755);

        snprintf(path, sizeof(path), "%s/%s/state", CONTAINER_STATE_DIR, id);
        FILE *f = fopen(path, "w");
        if (f) {
            time_t now = time(NULL);
            fprintf(f, "pid=%d\n", pid);
            fprintf(f, "hostname=%s\n", config->hostname);
            fprintf(f, "command=%s\n", config->argv[0]);
            fprintf(f, "image=%s\n", config->image ? config->image : "rootfs");
            fprintf(f, "started=%ld\n", now);
            fprintf(f, "memory=%ld\n", config->memory_limit / (1024 * 1024));
            fprintf(f, "cpu=%d\n", config->cpu_percent);
            fprintf(f, "net=%d\n", config->enable_net);
            fclose(f);
        }
    }

    static void remove_container_state(const char *id) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s/state", CONTAINER_STATE_DIR, id);
        unlink(path);
        snprintf(path, sizeof(path), "%s/%s", CONTAINER_STATE_DIR, id);
        rmdir(path);
    }

    // ---- KOMUT: run ----
    static int cmd_run(int argc, char *argv[]) {
        container_config config;
        config.rootfs = "./rootfs";
        config.hostname = "mini-container";
        config.image = NULL;
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
            } else if (strcmp(argv[i], "--hostname") == 0 && i + 1 < argc) {
                config.hostname = argv[++i];
                cmd_start = i + 1;
            } else if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
                config.image = argv[++i];
                char imgpath[PATH_MAX];
                snprintf(imgpath, sizeof(imgpath), "%s/%s", IMAGES_DIR, config.image);
                config.rootfs = strdup(imgpath);
                cmd_start = i + 1;
            } else {
                break;
            }
        }

        if (cmd_start >= argc) {
            fprintf(stderr, "Hata: Komut belirtilmedi\n");
            return 1;
        }

        config.argc = argc - cmd_start;
        config.argv = &argv[cmd_start];

        // Container ID olustur
        char container_id[13];
        generate_id(container_id, 13);

        printf("=== Mini Container Runtime ===\n");
        printf("[ANA PROCESS] Container ID: %s\n", container_id);
        printf("[ANA PROCESS] PID: %d\n", getpid());

        char *stack = malloc(STACK_SIZE);
        if (!stack) { perror("Stack ayirma basarisiz"); return 1; }

        int clone_flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
        if (config.enable_net) clone_flags |= CLONE_NEWNET;

        pid_t child_pid = clone(container_main, stack + STACK_SIZE, clone_flags, &config);
        if (child_pid < 0) {
            perror("clone basarisiz");
            free(stack);
            return 1;
        }

        printf("[ANA PROCESS] Container PID: %d\n", child_pid);

        // Durumu kaydet
        save_container_state(container_id, child_pid, &config);

        if (config.memory_limit > 0 || config.cpu_percent > 0 || config.max_pids > 0)
            setup_cgroups(child_pid, &config);

        if (config.enable_net)
            setup_network_host(child_pid);

        int status;
        waitpid(child_pid, &status, 0);

        printf("==========================================\n");
        printf("[ANA PROCESS] Container %s sonlandi (cikis kodu: %d)\n",
               container_id, WEXITSTATUS(status));

        if (config.enable_net) cleanup_network();
        cleanup_cgroups();
        remove_container_state(container_id);
        free(stack);
        return 0;
    }

    // ---- KOMUT: ps ----
    static int cmd_ps(void) {
        DIR *dir = opendir(CONTAINER_STATE_DIR);
        if (!dir) {
            printf("Calisan container yok.\n");
            return 0;
        }

        printf("%-14s %-8s %-20s %-12s %-8s %-6s %-4s\n",
               "CONTAINER ID", "PID", "HOSTNAME", "COMMAND", "IMAGE", "MEM", "NET");
        printf("%-14s %-8s %-20s %-12s %-8s %-6s %-4s\n",
               "------------", "---", "--------", "-------", "-----", "---", "---");

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;

            char path[PATH_MAX];
            snprintf(path, sizeof(path), "%s/%s/state", CONTAINER_STATE_DIR, entry->d_name);

            FILE *f = fopen(path, "r");
            if (!f) continue;

            int pid = 0, cpu = 0, net = 0;
            long mem = 0;
            char hostname[64] = "", command[64] = "", image[64] = "";

            char line[256];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\n")] = 0;
                if (strncmp(line, "pid=", 4) == 0) pid = atoi(line + 4);
                else if (strncmp(line, "hostname=", 9) == 0) strncpy(hostname, line + 9, 63);
                else if (strncmp(line, "command=", 8) == 0) strncpy(command, line + 8, 63);
                else if (strncmp(line, "image=", 6) == 0) strncpy(image, line + 6, 63);
                else if (strncmp(line, "memory=", 7) == 0) mem = atol(line + 7);
                else if (strncmp(line, "cpu=", 4) == 0) cpu = atoi(line + 4);
                else if (strncmp(line, "net=", 4) == 0) net = atoi(line + 4);
            }
            fclose(f);

            // Process hala calisiyor mu kontrol et
            char proc_path[64];
            snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);
            struct stat st;
            if (stat(proc_path, &st) != 0) {
                remove_container_state(entry->d_name);
                continue;
            }

            char mem_str[16], net_str[8];
            if (mem > 0) snprintf(mem_str, sizeof(mem_str), "%ldMB", mem);
            else strcpy(mem_str, "-");
            strcpy(net_str, net ? "yes" : "no");

            printf("%-14s %-8d %-20s %-12s %-8s %-6s %-4s\n",
                   entry->d_name, pid, hostname, command, image, mem_str, net_str);
        }
        closedir(dir);
        return 0;
    }

    // ---- KOMUT: stop ----
    static int cmd_stop(const char *id) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s/state", CONTAINER_STATE_DIR, id);

        FILE *f = fopen(path, "r");
        if (!f) {
            fprintf(stderr, "Container bulunamadi: %s\n", id);
            return 1;
        }

        int pid = 0;
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "pid=", 4) == 0) {
                pid = atoi(line + 4);
                break;
            }
        }
        fclose(f);

        if (pid > 0) {
            printf("Container %s durduruluyor (PID: %d)...\n", id, pid);
            kill(pid, SIGKILL);
            remove_container_state(id);
            printf("Container %s durduruldu.\n", id);
        }
        return 0;
    }

    // ---- KOMUT: images ----
    static int cmd_images(void) {
        printf("%-20s %-10s %-20s\n", "IMAGE", "BOYUT", "KONUM");
        printf("%-20s %-10s %-20s\n", "-----", "-----", "-----");

        // Varsayilan rootfs
        struct stat st;
        if (stat("rootfs", &st) == 0) {
            printf("%-20s %-10s %-20s\n", "default", "-", "./rootfs");
        }

        // images/ dizinindeki imajlar
        DIR *dir = opendir(IMAGES_DIR);
        if (!dir) return 0;

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') continue;
            char imgpath[PATH_MAX];
            snprintf(imgpath, sizeof(imgpath), "%s/%s", IMAGES_DIR, entry->d_name);
            if (stat(imgpath, &st) == 0 && S_ISDIR(st.st_mode)) {
                printf("%-20s %-10s %s\n", entry->d_name, "-", imgpath);
            }
        }
        closedir(dir);
        return 0;
    }

    // ---- KOMUT: create-image ----
    static int cmd_create_image(const char *name) {
        char imgpath[PATH_MAX];
        snprintf(imgpath, sizeof(imgpath), "%s/%s", IMAGES_DIR, name);

        mkdir(IMAGES_DIR, 0755);

        if (mkdir(imgpath, 0755) < 0 && errno != EEXIST) {
            perror("Image dizini olusturulamadi");
            return 1;
        }

        // rootfs'i kopyala
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "cp -a rootfs/* %s/", imgpath);
        printf("Image olusturuluyor: %s\n", name);
        if (run_cmd(cmd) != 0) {
            fprintf(stderr, "Image olusturulamadi\n");
            return 1;
        }

        printf("Image '%s' olusturuldu: %s\n", name, imgpath);
        return 0;
    }

    // ---- KULLANIM BILGISI ----
    static void print_usage(void) {
        printf("Mini Container Runtime\n\n");
        printf("Kullanim:\n");
        printf("  sudo ./container run [secenekler] <komut>   Container baslat\n");
        printf("  sudo ./container ps                         Calisan container'lari listele\n");
        printf("  sudo ./container stop <id>                  Container durdur\n");
        printf("  sudo ./container images                     Image'lari listele\n");
        printf("  sudo ./container create-image <isim>        Yeni image olustur\n");
        printf("\nSecenekler (run):\n");
        printf("  --memory <MB>       Bellek limiti\n");
        printf("  --cpu <yuzde>       CPU limiti\n");
        printf("  --pids <sayi>       Maks process sayisi\n");
        printf("  --net               Ag izolasyonu\n");
        printf("  --hostname <isim>   Container hostname\n");
        printf("  --image <isim>      Kullanilacak image\n");
        printf("\nOrnekler:\n");
        printf("  sudo ./container run /bin/sh\n");
        printf("  sudo ./container run --hostname webserver --net --memory 128 /bin/sh\n");
        printf("  sudo ./container run --image alpine-custom /bin/sh\n");
    }

    // ---- ANA PROGRAM ----
    int main(int argc, char *argv[]) {
        if (argc < 2) {
            print_usage();
            return 1;
        }

        if (strcmp(argv[1], "run") == 0) {
            return cmd_run(argc, argv);
        } else if (strcmp(argv[1], "ps") == 0) {
            return cmd_ps();
        } else if (strcmp(argv[1], "stop") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Kullanim: sudo ./container stop <container-id>\n");
                return 1;
            }
            return cmd_stop(argv[2]);
        } else if (strcmp(argv[1], "images") == 0) {
            return cmd_images();
        } else if (strcmp(argv[1], "create-image") == 0) {
            if (argc < 3) {
                fprintf(stderr, "Kullanim: sudo ./container create-image <isim>\n");
                return 1;
            }
            return cmd_create_image(argv[2]);
        } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
            print_usage();
            return 0;
        } else {
            fprintf(stderr, "Bilinmeyen komut: %s\n", argv[1]);
            print_usage();
            return 1;
        }
    }
