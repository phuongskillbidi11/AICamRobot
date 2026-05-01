# Tests: OTA Update Page UI Improvement

Run each test after completing the corresponding task. Stop and report on first
failure. Hardware required for T3 onwards (flash + AP connection).

---

## T1 — Build compiles without errors (after Task 3)

```bash
. ~/esp/esp-idf/export.sh
idf.py build 2>&1 | tail -5
```

**Pass:** last line contains `Project build complete.`  
**Fail:** any `error:` line — copy the exact error and report it.

---

## T2 — Static assets resolve (browser DevTools)

After flashing, open `http://192.168.4.1/update` in a desktop browser.
Open DevTools → Network tab. Reload.

**Pass:** ALL of the following return HTTP 200:
```
/bootstrap.min.css
/bootstrap.bundle.min.js
/bootstrap-icons.min.css
/fonts/bootstrap-icons.woff2
```
**Fail:** any asset returns 404 — the asset handler is not registered or the
path in the HTML does not match the URI registered in `app_httpd.cpp`.

---

## T3 — Page renders correctly (visual check)

Open `http://192.168.4.1/update`.

**Pass criteria (all must be true):**
- [ ] Page background uses Bootstrap body color (not plain white in dark mode)
- [ ] Card with header "Firmware OTA Update" is visible and centred
- [ ] Drop-zone border is dashed
- [ ] "Flash Firmware" button is **disabled** (greyed out) before file selection
- [ ] Bootstrap Icons render (cloud-upload icon visible in card header)
- [ ] No console errors (DevTools → Console)

**Fail:** screenshot the issue and report which criterion failed.

---

## T4 — Invalid file rejected before upload

1. Click the drop-zone or "Choose firmware" area.
2. Select any `.txt` or `.jpg` file.

**Pass:** A red Bootstrap alert appears: "Please select a valid .bin firmware
file." No XHR request is made (verify in DevTools → Network: no POST to
`/ota/upload`).  
**Fail:** The file is accepted or no error is shown.

---

## T5 — Valid upload with progress bar

> You do not need a real firmware for this test — any large `.bin`-renamed file
> works to verify the progress UI (the server will reject it after OTA
> validation, but the upload progress will be visible).

1. Rename a large file (>1 MB) to `test.bin`.
2. Select it in the drop-zone.
3. Confirm `file-name` span updates: `test.bin  (X.XX MB)`.
4. Confirm "Flash Firmware" button becomes **enabled**.
5. Click "Flash Firmware".

**Pass:**
- [ ] Upload progress section appears (not `d-none`)
- [ ] Bootstrap progress bar fills from 0% to 100% as data is sent
- [ ] Percentage label updates (e.g., `34%  —  0.4 / 1.1 MB`)
- [ ] Progress bar disappears when the server responds

**Fail:** progress never appears, or jumps straight to 100% without intermediate
values.

---

## T6 — Successful OTA flash

Use the real firmware binary: `build/AICamRobot.bin`.

1. Select `build/AICamRobot.bin` in the drop-zone.
2. Click "Flash Firmware".
3. Wait for upload to complete.

**Pass:**
- [ ] Green alert appears: "✓ Flash successful! Device is rebooting…"
- [ ] Countdown section appears: "Redirecting in 10 s…"
- [ ] Countdown bar shrinks over 10 seconds
- [ ] At 0, browser redirects to `http://192.168.4.1/`
- [ ] Main camera page loads (confirms device rebooted to new firmware)

**Fail:** any step above does not occur — note which step and the exact text of
any error alert.

---

## T7 — Server-side rejection (invalid binary)

1. Create a dummy file: `echo "not a firmware" > fake.bin`
2. Upload `fake.bin` via the OTA page.

**Pass:** Red alert appears containing the server error text
(`"Invalid image"` or `"OTA end failed"`). "Flash Firmware" button re-enables.  
**Fail:** Page shows success, or shows no message, or crashes.

---

## T8 — Mobile responsive check (375 px width)

Open `http://192.168.4.1/update` in Chrome DevTools device toolbar at
iPhone SE size (375 × 667).

**Pass:**
- [ ] Card fits within viewport (no horizontal scroll)
- [ ] Drop-zone text is readable
- [ ] "Flash Firmware" button spans full card width
- [ ] Progress bar visible and correctly proportioned

**Fail:** horizontal scroll appears, or any element overflows the viewport.

---

## T9 — Dark mode

On the page, use the theme dropdown to switch to **Dark**.

**Pass:**
- [ ] Background, card, text, and progress bar all use dark Bootstrap theme
- [ ] Drop-zone dashed border visible in dark mode
- [ ] Selection persists on page reload (stored in `localStorage`)

**Fail:** colors do not change, or page breaks visually in dark mode.

---

## T10 — Back link works

Click `← AI Cam S3` in the top navbar.

**Pass:** Browser navigates to `http://192.168.4.1/` and the main camera
dashboard loads.  
**Fail:** 404, blank page, or no navigation occurs.
