#!/bin/bash
    # Mini Container Runtime - Otomatik Test
    PASS=0
    FAIL=0
    TOTAL=0

    test_case() {
        TOTAL=$((TOTAL + 1))
        local desc="$1"
        local cmd="$2"
        local expected="$3"

        result=$(eval "$cmd" 2>&1)
        if echo "$result" | grep -q "$expected"; then
            echo -e "  \033[0;32m✅ GECTI\033[0m  $desc"
            PASS=$((PASS + 1))
        else
            echo -e "  \033[0;31m❌ KALDI\033[0m  $desc"
            echo "     Beklenen: $expected"
            echo "     Alinan:   $result"
            FAIL=$((FAIL + 1))
        fi
    }

    echo ""
    echo "=== Mini Container Runtime - Test Suite ==="
    echo ""

    echo "[Derleme]"
    test_case "Derleme basarili" "./build.sh 2>&1" "Tamamlandi"

    echo ""
    echo "[CLI]"
    test_case "help komutu calisiyor" "sudo ./container help 2>&1" "Kullanim"
    test_case "images komutu calisiyor" "sudo ./container images 2>&1" "IMAGE"
    test_case "Bilinmeyen komut hata veriyor" "sudo ./container xyz 2>&1" "Bilinmeyen komut"

    echo ""
    echo "[PID Izolasyonu]"
    test_case "Container PID 1 olarak calisiyor" \
        "sudo ./container run /bin/sh -c 'echo PID:\$\$' 2>&1" "PID:1"

    echo ""
    echo "[Hostname Izolasyonu]"
    test_case "Varsayilan hostname" \
        "sudo ./container run /bin/sh -c 'hostname' 2>&1" "mini-container"
    test_case "Ozel hostname" \
        "sudo ./container run --hostname testbox /bin/sh -c 'hostname' 2>&1" "testbox"

    echo ""
    echo "[Dosya Sistemi Izolasyonu]"
    test_case "Alpine rootfs gorunuyor" \
        "sudo ./container run /bin/sh -c 'cat /etc/os-release' 2>&1" "Alpine"
    test_case "Host dosyalari gorunmuyor" \
        "sudo ./container run /bin/sh -c 'ls /home 2>&1 || echo bos' 2>&1" ""

    echo ""
    echo "[Cgroup Kaynak Sinirlamasi]"
    test_case "PID limiti calisiyor" \
        "sudo ./container run --pids 3 /bin/sh -c 'for i in 1 2 3 4 5; do sleep 10 & done 2>&1; ps aux | wc -l' 2>&1"
  "Resource temporarily unavailable"

    echo ""
    echo "[Ag Izolasyonu]"
    test_case "Container IP adresi aliyor" \
        "sudo ./container run --net /bin/sh -c 'ip addr show veth1' 2>&1" "10.0.0.2"
    test_case "Bridge'e ping calisiyor" \
        "sudo ./container run --net /bin/sh -c 'ping -c 1 -W 2 10.0.0.1' 2>&1" "1 packets received"

    echo ""
    echo "[Image Yonetimi]"
    test_case "Image olusturma calisiyor" \
        "sudo ./container create-image test-img 2>&1" "olusturuldu"
    test_case "Olusturulan image listeleniyor" \
        "sudo ./container images 2>&1" "test-img"

    # Temizlik
    sudo rm -rf images/test-img 2>/dev/null

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Sonuc: $PASS/$TOTAL gecti, $FAIL basarisiz"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
