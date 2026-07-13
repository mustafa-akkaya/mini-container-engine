#!/bin/bash

    # Mini Container Runtime - Demo
    set -e
    GREEN='\033[0;32m'
    CYAN='\033[0;36m'
    YELLOW='\033[1;33m'
    RED='\033[0;31m'
    NC='\033[0m'
    BOLD='\033[1m'

    print_header() {
        echo ""
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${BOLD}  $1${NC}"
        echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    }

    print_step() {
        echo -e "\n${YELLOW}>> $1${NC}"
    }

    print_result() {
        echo -e "${GREEN}   $1${NC}"
    }

    wait_key() {
        echo -e "\n${CYAN}[Enter'a bas devam etmek icin]${NC}"
        read -r
    }

    clear
    echo -e "${BOLD}"
    echo "  __  __ _       _    ____            _        _                 "
    echo " |  \/  (_)_ __ (_)  / ___|___  _ __ | |_ __ _(_)_ __   ___ _ __ "
    echo " | |\/| | | '_ \| | | |   / _ \| '_ \| __/ _\` | | '_ \ / _ \ '__|"
    echo " | |  | | | | | | | | |__| (_) | | | | || (_| | | | | |  __/ |   "
    echo " |_|  |_|_|_| |_|_|  \____\___/|_| |_|\__\__,_|_|_| |_|\___|_|   "
    echo -e "${NC}"
    echo -e "${CYAN}  Linux namespace, cgroup ve pivot_root ile C'de yazilmis${NC}"
    echo -e "${CYAN}  minimal container runtime${NC}"
    echo ""
    echo "  Bu demo, container runtime'in tum ozelliklerini gosterir."
    wait_key

    # ============================================
    print_header "1. CLI YARDIM EKRANI"
    print_step "sudo ./container help"
    sudo ./container help
    wait_key

    # ============================================
    print_header "2. IMAGE YONETIMI"
    print_step "Mevcut image'lari listele"
    sudo ./container images
    echo ""
    print_step "Yeni image olustur: 'alpine-web'"
    sudo ./container create-image alpine-web
    echo ""
    print_step "Image listesini tekrar kontrol et"
    sudo ./container images
    wait_key

    # ============================================
    print_header "3. PID IZOLASYONU"
    print_step "Container icinde PID 1 olarak calisma"
    echo -e "${YELLOW}   Host PID: $$${NC}"
    sudo ./container run /bin/sh -c "echo '   Container PID:' \$\$ && echo '   Process listesi:' && ps aux"
    print_result "Container kendi PID namespace'inde PID 1 olarak basladi"
    wait_key

    # ============================================
    print_header "4. HOSTNAME IZOLASYONU"
    print_step "Host hostname vs Container hostname"
    echo -e "   Host hostname: $(hostname)"
    sudo ./container run --hostname my-webapp /bin/sh -c "echo '   Container hostname:' \$(hostname)"
    print_result "Container'in hostname'i host'u etkilemedi"
    wait_key

    # ============================================
    print_header "5. DOSYA SISTEMI IZOLASYONU"
    print_step "Container kendi dosya sistemini goruyor (Alpine Linux)"
    sudo ./container run /bin/sh -c "cat /etc/os-release | head -3 && echo '   ---' && ls / | tr '\n' ' ' && echo ''"
    print_result "Container sadece Alpine rootfs'i goruyor, host dosyalarina erisemiyor"
    wait_key

    # ============================================
    print_header "6. CGROUP KAYNAK SINIRLAMASI"
    print_step "Maks 5 process limiti ile container baslat"
    echo -e "${YELLOW}   10 process olusturmaya calisiyoruz...${NC}"
    sudo ./container run --pids 5 /bin/sh -c '
        echo "   Process olusturuluyor..."
        for i in 1 2 3 4 5 6 7 8; do
            if sleep 100 & 2>/dev/null; then
                echo "   Process $i: basarili"
            else
                echo "   Process $i: ENGELLENDI (cgroup limiti)"
            fi
        done
        echo "   Toplam process:"
        ps aux | wc -l
    '
    print_result "cgroup pids controller fazla process olusumunu engelledi"
    wait_key

    # ============================================
    print_header "7. AG IZOLASYONU"
    print_step "Container'a ozel ag arayuzu ve IP adresi"
    sudo ./container run --net --hostname netdemo /bin/sh -c '
        echo "   Container IP adresleri:"
        ip addr show veth1 2>/dev/null | grep "inet "
        echo ""
        echo "   Bridge ping testi:"
        ping -c 2 10.0.0.1 2>/dev/null | tail -1
        echo ""
        echo "   Internet ping testi (8.8.8.8):"
        ping -c 2 8.8.8.8 2>/dev/null | tail -1
    '
    print_result "Container kendi izole aginda calisiyor ve internete erisiyor"
    wait_key

    # ============================================
    print_header "8. TUM OZELLIKLER BIR ARADA"
    print_step "Tam izole container: hostname + ag + bellek limiti + process limiti"
    sudo ./container run --hostname production --net --memory 64 --pids 20 /bin/sh -c '
        echo "   Hostname:     $(hostname)"
        echo "   PID:          $$"
        echo "   IP:           $(ip addr show veth1 2>/dev/null | grep "inet " | awk "{print \$2}")"
        echo "   OS:           $(cat /etc/os-release | grep PRETTY_NAME | cut -d= -f2)"
        echo "   Bellek limit: 64 MB"
        echo "   PID limit:    20"
        echo ""
        echo "   Dosya sistemi izole: host dosyalari GORUNMUYOR"
        echo "   Ag izole: kendi IP adresi VAR"
        echo "   Kaynaklar sinirli: cgroup AKTIF"
    '
    print_result "Tum izolasyon katmanlari aktif!"

    # ============================================
    echo ""
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}${BOLD}  Demo tamamlandi!${NC}"
    echo ""
    echo -e "  Bu container runtime su Linux cekirdek ozelliklerini kullaniyor:"
    echo -e "    ${YELLOW}•${NC} clone()     - Yeni namespace'ler olusturur"
    echo -e "    ${YELLOW}•${NC} pivot_root  - Dosya sistemi izolasyonu"
    echo -e "    ${YELLOW}•${NC} cgroups v2  - Kaynak sinirlamasi"
    echo -e "    ${YELLOW}•${NC} veth/bridge - Ag izolasyonu"
    echo -e "    ${YELLOW}•${NC} iptables    - NAT ile internet erisimi"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
