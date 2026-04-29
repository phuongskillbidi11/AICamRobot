# Skill: Web Streaming & REST API

## Purpose
Serve live MJPEG video from the camera over HTTP and expose a REST control API for camera settings and AI features. The web UI is embedded in the firmware as gzip-compressed HTML.

## When to use
- Accessing the camera stream from a browser
- Controlling camera parameters (resolution, quality, flip, etc.) programmatically
- Modifying or replacing the web UI
- Adding new REST endpoints
- Debugging stream disconnects or HTTP 500 errors

## Files involved
- `components/modules/web/app_httpd.cpp/.hpp` — all HTTP handlers and server start
- `components/modules/web/www/index_ov2640.html(.gz)` — embedded UI for OV2640
- `components/modules/web/www/index_ov3660.html(.gz)` — embedded UI for OV3660
- `components/modules/web/www/index_ov5640.html(.gz)` — embedded UI for OV5640
- `components/modules/web/www/monitor.html(.gz)` — embedded multi-camera monitor UI
- `components/modules/web/app_mdns.c` — provides `/mdns` endpoint data
- `components/modules/CMakeLists.txt` — `EMBED_FILES` directive compiles `.gz` files into flash

## HTTP server layout

Two `httpd_handle_t` instances are created:

| Server | Port | Purpose |
|--------|------|---------|
| `camera_httpd` | **80** | Control REST API + UI serving |
| `stream_httpd` | **81** | MJPEG live stream |

### Accessing the stream
```
http://<device-ip>/stream          # MJPEG multipart stream (port 80 redirects here)
http://<device-ip>:81/stream       # direct stream endpoint
```
Stream content type: `multipart/x-mixed-replace;boundary=123456789000000000000987654321`
Each part: `Content-Type: image/jpeg` + `X-Timestamp: sec.usec`

## REST API reference (port 80)

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Serve embedded HTML UI (sensor-specific) |
| `/capture` | GET | Single JPEG snapshot |
| `/stream` | GET | MJPEG stream (same as port 81) |
| `/cmd` | GET | Set camera register/parameter |
| `/status` | GET | JSON of all camera sensor registers |
| `/xclk` | GET | Change XCLK frequency (`?xclk=<MHz>`) |
| `/reg` | GET | Read/write sensor register (`?reg=&mask=&val=`) |
| `/mdns` | GET | JSON list of discovered cameras on LAN |

### `/cmd` parameter list (`?var=<name>&val=<int>`)
| var | Effect |
|-----|--------|
| `framesize` | Set frame size (enum value) |
| `quality` | JPEG quality 0–63 |
| `contrast` | −2 to +2 |
| `brightness` | −2 to +2 |
| `saturation` | −2 to +2 |
| `awb` | Auto white balance on/off |
| `agc` | Auto gain control on/off |
| `aec` | Auto exposure control on/off |
| `hmirror` | Horizontal mirror on/off |
| `vflip` | Vertical flip on/off |
| `face_detect` | Enable (1) / disable (0) face detection overlay |
| `face_enroll` | Trigger (1) face enrollment (recognition module only) |
| `face_recognize` | Enable (1) / disable (0) face recognition |

## Modifying or rebuilding the web UI

Source HTML files are in `components/modules/web/www/`. After editing:
```bash
cd components/modules/web/www
bash compress_pages.sh     # regenerates the .gz files
```
Then rebuild the firmware — the `.gz` files are embedded via `EMBED_FILES` in `CMakeLists.txt` and referenced as `index_ov2640_html_gz` symbols.

## Frame flow through HTTP
```
xQueueHttpFrame → stream_handler (or capture_handler)
    → frame2jpg() if not already JPEG
    → httpd_resp_send_chunk() per multipart frame
    → esp_camera_fb_return(frame)   ← frame released here
```
`gReturnFB` flag (set when `register_httpd` is called with `true`) controls whether `app_httpd` returns the frame or delegates upstream.

## Advanced / cross-references
- The stream handler loops indefinitely until the client disconnects; `res != ESP_OK` breaks the loop.
- `X-Framerate: 60` header is set as a hint but actual FPS depends on inference latency.
- All endpoints set `Access-Control-Allow-Origin: *` — CORS is fully open by design.
- To add a new endpoint: add a `httpd_uri_t` struct and call `httpd_register_uri_handler(camera_httpd, &your_uri)` in `register_httpd()`.
- See **skill: wifi-network** for how the device obtains its IP and hostname.
- See **skill: face-recognition** for the `face_enroll`/`face_recognize` REST commands.
