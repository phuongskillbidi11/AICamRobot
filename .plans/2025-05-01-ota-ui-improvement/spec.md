# Spec: OTA Update Page UI Improvement

## Goal

Replace the current minimal `OTA_PAGE[]` C string in `app_httpd.cpp` with a
professionally styled, Bootstrap-based page that matches the main dashboard UI
(`index_ov2640.html`) while keeping the identical server-side OTA logic
(`POST /ota/upload`, `esp_ota_begin/write/end`, delayed reboot task).

---

## Key discovery: Bootstrap is already served

The following assets are already embedded in ESP32 flash and served by the HTTP
server (see `app_httpd.cpp` handlers and `CMakeLists.txt` EMBED_FILES):

| URL | Handler | Size (gz) |
|-----|---------|-----------|
| `/bootstrap.min.css` | `bootstrap_css_handler` | ~31 KB |
| `/bootstrap.bundle.min.js` | `bootstrap_js_handler` | ~24 KB |
| `/bootstrap-icons.min.css` | `bootstrap_icons_css_handler` | ~13 KB |
| `/fonts/bootstrap-icons.woff2` | `bootstrap_icons_font_handler` | 131 KB |

The new OTA page links directly to these paths. **No new embedded files are
needed.** No CDN. Works in AP mode with no internet.

---

## Design

### Layout

Single-column centered card, max-width 540 px, vertically centered.
Matches the sidebar dashboard's color palette and dark/light mode.

```
┌─────────────────────────────────────┐
│  ← Back to camera        [Light|Auto|Dark] │  ← top bar (minimal)
├─────────────────────────────────────┤
│  🔄  Firmware OTA Update            │  ← card header (primary)
│─────────────────────────────────────│
│  Device: AI Cam S3  │  Partition: ota_1  │  ← info row
│─────────────────────────────────────│
│  ┌──────────────────────────────┐   │
│  │   Drag .bin here or click    │   │  ← drag-drop zone
│  │   [  Choose firmware  ]      │   │
│  └──────────────────────────────┘   │
│                                     │
│  [    Flash Firmware    ]           │  ← primary button
│                                     │
│  ████████████░░░░░░░  62%  1.2 MB  │  ← Bootstrap progress bar (hidden until upload)
│                                     │
│  ✓ Success! Rebooting in 8 s…      │  ← status (hidden until result)
│  [████████░░] countdown bar         │
└─────────────────────────────────────┘
```

### Color scheme

Reuse Bootstrap 5 semantic classes only — no hardcoded hex colors.
Dark/light mode handled by `data-bs-theme` on `<html>`, toggled by a
dropdown identical to the main dashboard (same inline color-modes script).

### Behavior

1. **File selection**: `<input type="file" accept=".bin">` hidden behind a
   styled drop-zone `<label>`. Drag-and-drop also accepted via `dragover` /
   `drop` events on the zone.
2. **Validation before upload**: check `file.name.endsWith('.bin')` and
   `file.size > 0`. Show inline error badge if invalid; do not POST.
3. **Upload**: XHR POST to `/ota/upload` with `Content-Type:
   application/octet-stream`, body = raw file bytes (unchanged from current
   implementation).
4. **Progress**: `xhr.upload.onprogress` updates a Bootstrap `.progress-bar`
   (width %) and a `<span>` showing `XX% — Y.Y MB / Z.Z MB`.
5. **Success**: hide progress bar; show green alert with a 10-second countdown
   `<span id="cd">10</span>` ticking down via `setInterval`. When it reaches 0,
   redirect to `/`. The countdown is also shown as a second Bootstrap progress
   bar shrinking from 100 → 0.
6. **Error**: show red alert with the exact response text from the server.
   Re-enable the Flash button so the user can retry.
7. **Connection loss**: if `xhr.onerror` fires (device rebooted mid-upload),
   show a warning alert: "Connection lost — device may be rebooting."

### Responsive

`max-width: 540px; margin: auto` on the card. Bootstrap grid not needed.
Tested at 375 px (iPhone SE) and 768 px (iPad).

---

## Files changed

| File | Change |
|------|--------|
| `components/modules/web/app_httpd.cpp` | Replace `OTA_PAGE[]` C string (lines 795-847) with new HTML. No other changes. |

No new handlers, no new embedded assets, no CMakeLists changes.

---

## Out of scope

- Serving the OTA page as a separate `.html.gz` embedded file (unnecessary given
  the page is small and self-contained).
- Any change to `ota_upload_handler`, `ota_reboot_task`, or partition logic.
- Rollback / version display (future enhancement).
