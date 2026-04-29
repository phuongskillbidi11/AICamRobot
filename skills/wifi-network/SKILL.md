# Skill: WiFi & mDNS

## Purpose
Connect the ESP32-S3 to an existing WiFi network (STA mode), host its own access point (AP mode), or both simultaneously (APSTA mode). Advertise the camera over mDNS so it can be discovered by name on the LAN.

## When to use
- Configuring network credentials before the first deployment
- Debugging connectivity failures (no IP, retries exhausted)
- Discovering the device IP without a serial monitor
- Understanding how multiple cameras are discovered via mDNS

## Files involved
- `components/modules/web/app_wifi.c` — WiFi init, event handling, mode selection
- `components/modules/web/app_mdns.c` — mDNS init, service registration, camera discovery
- `components/modules/web/app_wifi.h` / `app_mdns.h` — public headers
- `components/modules/Kconfig` — WiFi config keys (see **skill: menuconfig**)

## WiFi modes

`app_wifi_main()` decides the mode at runtime based on which Kconfig strings are non-empty:

| `ESP_WIFI_SSID` | `ESP_WIFI_AP_SSID` | Mode |
|-----------------|---------------------|------|
| set | set | `WIFI_MODE_APSTA` |
| empty | set | `WIFI_MODE_AP` |
| set | empty | `WIFI_MODE_STA` |
| empty | empty | WiFi disabled |

### STA mode
- Connects to `ESP_WIFI_SSID` with `ESP_WIFI_PASSWORD`.
- Retries up to `ESP_MAXIMUM_RETRY` (default 5) times before setting `WIFI_FAIL_BIT`.
- `app_wifi_main()` **blocks** until `WIFI_CONNECTED_BIT` or `WIFI_FAIL_BIT` is set — the rest of the app does not start until WiFi resolves.
- Power save is explicitly disabled: `esp_wifi_set_ps(WIFI_PS_NONE)` — ensures low-latency streaming.

### AP mode
- SSID: `ESP_WIFI_AP_SSID` (default `ESP32-Camera`)
- Password: `ESP_WIFI_AP_PASSWORD` (empty = open network, `WIFI_AUTH_OPEN`)
- Default IP: `192.168.4.1` (configurable via `SERVER_IP`)
- Max connections: 4 (hardcoded override of Kconfig `MAX_STA_CONN`)
- If `SERVER_IP` differs from `192.168.4.1`, the function reconfigures the DHCP server IP.

## mDNS discovery

`app_mdns_main()` must be called **after** `register_camera()` (needs the sensor PID):

```c
app_wifi_main();
register_camera(...);
app_mdns_main();          // ← after camera init
register_human_face_detection(...);
register_httpd(...);
```

### Service registration
Two services are registered:
1. `_http._tcp` on port 80 — standard HTTP service
2. `_esp-cam._tcp` on port 80 — ESP camera discovery protocol with TXT records:

| TXT key | Value |
|---------|-------|
| `board` | Camera module name (e.g., `ESP-S3-WROOM-CAM`) |
| `model` | Sensor model (e.g., `OV2640`) |
| `stream_port` | `81` |
| `framesize` | Current frame size enum value |
| `pixformat` | Current pixel format enum value |

### Hostname
- If `ESP_HOST_NAME` is set in menuconfig: uses that value
- Otherwise: auto-generated as `<BOARD>-<MODEL>-<last 3 MAC bytes>` (lowercase), e.g., `esp-s3-wroom-cam-ov2640-a1b2c3`

### Camera discovery
`app_mdns_main()` spawns a background task that queries `_esp-cam._tcp` every 55 seconds and stores discovered cameras. The `/mdns` HTTP endpoint returns the full list as JSON.

## Checking connectivity
```bash
# From the serial monitor — look for:
# I (xxxx) camera wifi: got ip:192.168.x.x

# Ping by mDNS hostname (Linux/Mac)
ping esp-s3-wroom-cam-ov2640-a1b2c3.local

# Or check your router's DHCP table for the device MAC
```

## Advanced / cross-references
- NVS flash is initialized inside `app_wifi_main()` (`nvs_flash_init()`). If NVS is corrupted, it auto-erases and re-initializes.
- The event loop (`esp_event_loop_create_default()`) is also created here — do not create it again elsewhere.
- See **skill: menuconfig** to set all WiFi credentials and AP parameters.
- See **skill: web-streaming** for the HTTP endpoints served after WiFi is up.
