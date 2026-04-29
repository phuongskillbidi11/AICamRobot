# Skill: Environment Setup

## Purpose
Get the ESP-IDF toolchain installed, sourced, and verified so that `idf.py` commands work correctly. ESP-IDF **v5.5 or later** is required — v5.2 fails at compile time due to missing `MALLOC_CAP_SIMD`.

## When to use
- First-time setup on a new machine or WSL instance
- After opening a new terminal (ESP-IDF environment is not persistent)
- When `idf.py` is not found or the wrong version is active
- When a build fails with `MALLOC_CAP_SIMD` or `esp_lcd_*` API errors

## Files involved
- `~/esp/esp-idf/` — ESP-IDF installation (standard path used by deploy.sh)
- `deploy.sh` — sources `~/esp/esp-idf/export.sh` automatically
- `sdkconfig.defaults` — defines target, flash size, PSRAM mode (do not change without reason)
- `sdkconfig.defaults.esp32s3` — S3-specific overrides
- `dependencies.lock` — component manager lockfile (do not edit manually)

## Step-by-step instructions

### 1. Install ESP-IDF v5.5 (first time only)
```bash
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.5          # or latest v5.x tag
git submodule update --init --recursive
./install.sh esp32s3
```

### 2. Source the environment (every new terminal)
```bash
. ~/esp/esp-idf/export.sh
```
Verify:
```bash
idf.py --version           # must show 5.5.x or higher
echo $IDF_PATH             # must be set to ~/esp/esp-idf
```

### 3. Install managed component dependencies
From the project root, the component manager runs automatically on the first `idf.py build`. To resolve manually:
```bash
cd ~/esp/AICamRobot
idf.py update-dependencies
```
This reads `idf_component.yml` files in each component and populates `managed_components/`.

### 4. Set the target (if sdkconfig is missing)
```bash
idf.py set-target esp32s3
```
This should be a no-op if `sdkconfig` already exists and matches `CONFIG_IDF_TARGET="esp32s3"`.

## Advanced / cross-references
- If you add a new managed component, run `idf.py update-dependencies` and commit the updated `dependencies.lock`.
- `sdkconfig.defaults` layering order: `sdkconfig.defaults` → `sdkconfig.defaults.esp32s3` → `sdkconfig`. Values in later files override earlier ones; `sdkconfig` takes final precedence after `menuconfig`.
- See **skill: build-flash** for the next step after sourcing the environment.
