# Skill: Build & Flash

## Purpose
Compile the AICamRobot firmware and flash it to an ESP32-S3-WROOM-CAM board.

## When to use
- After any source code change to build and test on hardware
- When deploying to a new board
- When you need to flash only specific binary segments (bootloader, partition table, app)

## Files involved
- `deploy.sh` — convenience script wrapping `idf.py build` + `esptool` flash + `idf.py monitor`
- `CMakeLists.txt` — project root; sets `EXTRA_COMPONENT_DIRS` to `components/`
- `partitions.csv` — custom partition layout (factory app at 0x10000, nvs, fr)
- `build/AICamRobot.bin` — compiled application binary
- `build/bootloader/bootloader.bin`
- `build/partition_table/partition-table.bin`

## Step-by-step instructions

### Prerequisites
Source ESP-IDF first (see **skill: environment-setup**):
```bash
. ~/esp/esp-idf/export.sh
```

### Option A — deploy.sh (recommended)
```bash
cd ~/esp/AICamRobot

# Build + flash + open monitor (default action = "all")
./deploy.sh /dev/ttyUSB0 460800

# Build only
./deploy.sh /dev/ttyUSB0 460800 build

# Flash only (uses pre-built binaries in build/)
./deploy.sh /dev/ttyUSB0 460800 flash

# Monitor only
./deploy.sh /dev/ttyUSB0 460800 monitor
```
Exit the monitor with **Ctrl+]**.

### Option B — idf.py directly
```bash
idf.py build
idf.py -p /dev/ttyUSB0 -b 460800 flash
idf.py -p /dev/ttyUSB0 monitor
```

### Option C — esptool manually (flash without IDF sourced)
```bash
python3 -m esptool --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0     build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/AICamRobot.bin
```

### Flash settings (fixed by sdkconfig.defaults)
| Parameter | Value |
|-----------|-------|
| Flash mode | DIO |
| Flash freq | 80 MHz |
| Flash size | 16 MB |
| App offset | 0x10000 |

### Clean build
```bash
idf.py fullclean
idf.py build
```

## Advanced / cross-references
- **Adding a new `.c`/`.cpp` file**: update `idf_component_register(SRCS ...)` or `SRC_DIRS` in the relevant `CMakeLists.txt` in the same change or the linker will fail silently.
- Compile options for `components/modules/` are set in `components/modules/CMakeLists.txt`: `-ffast-math -O3`. Do not override per-file without a clear reason.
- See **skill: menuconfig** to set WiFi credentials before flashing.
- See **skill: hardware-pins** if the board does not enumerate on `/dev/ttyUSB0`.
