#!/bin/bash
    echo "Derleniyor..."
    gcc -Wall -Wextra -std=c11 -o container main.c runtime.c
    echo "Tamamlandi: ./container"

