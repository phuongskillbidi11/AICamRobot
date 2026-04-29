#!/bin/bash

PORT=${1:-/dev/ttyUSB0}
BAUD=${2:-460800}

echo "=== ESP32-S3 Deploy Script ==="
echo "Port: $PORT"

# Source IDF
source ~/esp/esp-idf/export.sh

cd ~/esp/AICamRobot

case "${3:-all}" in
  build)
    echo ">>> Building..."
    idf.py build
    ;;
  flash)
    echo ">>> Flashing..."
    python3 -m esptool --chip esp32s3 -p $PORT -b $BAUD \
      --before default_reset --after hard_reset \
      write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
      0x0 build/bootloader/bootloader.bin \
      0x8000 build/partition_table/partition-table.bin \
      0x10000 build/AICamRobot.bin
    ;;
  monitor)
    echo ">>> Monitoring... (Ctrl+] to exit)"
    idf.py monitor -p $PORT
    ;;
  all|*)
    echo ">>> Building..."
    idf.py build || exit 1
    echo ">>> Flashing..."
    python3 -m esptool --chip esp32s3 -p $PORT -b $BAUD \
      --before default_reset --after hard_reset \
      write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
      0x0 build/bootloader/bootloader.bin \
      0x8000 build/partition_table/partition-table.bin \
      0x10000 build/AICamRobot.bin || exit 1
    echo ">>> Monitoring... (Ctrl+] to exit)"
    idf.py monitor -p $PORT
    ;;
esac
