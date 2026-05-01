# Tasks: OTA Update Page UI Improvement

Each task must be completed and its test (see `tests.md`) must pass before
moving to the next. Mark `[x]` when done.

---

## Task 1 — Write the new HTML string

- [x] **1.1** In `app_httpd.cpp`, locate `static const char OTA_PAGE[] =` at
  line ~795. Delete the entire string literal (lines 795–847, everything from
  the opening `=` to the closing `;`).

- [x] **1.2** Replace it with the new HTML block below. Use the same
  concatenated C string literal style. The HTML must:

  **Head section:**
  ```
  <!doctype html>
  <html lang="en" data-bs-theme="auto">
  <head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>OTA Update — AI Cam S3</title>

    <!-- inline color-modes (same script as index_ov2640.html) -->
    <script> ... (copy the color-modes IIFE from index_ov2640.html head) ... </script>

    <link href="/bootstrap.min.css" rel="stylesheet">
    <link href="/bootstrap-icons.min.css" rel="stylesheet">

    <style>
      /* drop-zone */
      #drop-zone {
        border: 2px dashed var(--bs-border-color);
        border-radius: 8px;
        padding: 32px 16px;
        text-align: center;
        cursor: pointer;
        transition: background .15s, border-color .15s;
      }
      #drop-zone.drag-over {
        background: var(--bs-primary-bg-subtle);
        border-color: var(--bs-primary);
      }
      #file-input { display: none; }
    </style>
  </head>
  ```

  **Body structure:**
  ```
  <body class="bg-body-tertiary d-flex flex-column min-vh-100">

    <!-- minimal top bar -->
    <nav class="navbar border-bottom bg-body-tertiary px-3">
      <a href="/" class="navbar-brand">
        <i class="bi bi-camera-video-fill text-primary me-1"></i>AI Cam S3
      </a>
      <!-- theme dropdown (same SVG icons + dropdown as index_ov2640.html) -->
    </nav>

    <!-- centered card -->
    <main class="flex-grow-1 d-flex align-items-center justify-content-center py-4 px-3">
      <div class="card shadow-sm w-100" style="max-width:540px">

        <div class="card-header bg-primary text-white">
          <i class="bi bi-cloud-upload me-2"></i>Firmware OTA Update
        </div>

        <div class="card-body">

          <!-- info row -->
          <div class="d-flex gap-3 mb-3 small text-muted">
            <span><i class="bi bi-cpu me-1"></i>AI Cam S3</span>
            <span id="part-info"><i class="bi bi-hdd me-1"></i>Ready</span>
          </div>

          <!-- drop zone -->
          <div id="drop-zone" onclick="document.getElementById('file-input').click()">
            <i class="bi bi-file-earmark-binary fs-2 text-muted"></i>
            <p class="mb-1 mt-2">Drag <code>.bin</code> here or click to choose</p>
            <p class="text-muted small mb-0" id="file-name">No file selected</p>
          </div>
          <input type="file" id="file-input" accept=".bin">

          <!-- flash button -->
          <button class="btn btn-primary w-100 mt-3" id="flash-btn" disabled>
            <i class="bi bi-lightning-charge-fill me-1"></i>Flash Firmware
          </button>

          <!-- upload progress (hidden until upload starts) -->
          <div id="upload-progress" class="mt-3 d-none">
            <div class="d-flex justify-content-between small mb-1">
              <span>Uploading…</span>
              <span id="pct-label">0%</span>
            </div>
            <div class="progress" style="height:10px">
              <div class="progress-bar progress-bar-striped progress-bar-animated"
                id="upload-bar" style="width:0%"></div>
            </div>
          </div>

          <!-- status alert (hidden until result) -->
          <div id="status-alert" class="alert mt-3 d-none" role="alert"></div>

          <!-- reboot countdown (hidden until success) -->
          <div id="reboot-section" class="mt-3 d-none">
            <div class="d-flex justify-content-between small mb-1">
              <span class="text-success">
                <i class="bi bi-check-circle-fill me-1"></i>
                Redirecting in <strong id="cd">10</strong> s…
              </span>
            </div>
            <div class="progress" style="height:6px">
              <div class="progress-bar bg-success" id="cd-bar" style="width:100%"></div>
            </div>
          </div>

        </div><!-- /card-body -->

        <div class="card-footer text-muted small">
          Upload <code>build/AICamRobot.bin</code> — the device will reboot after flashing.
        </div>

      </div><!-- /card -->
    </main>

    <script src="/bootstrap.bundle.min.js"></script>
    <script>
      // ... (JS logic — see Task 2) ...
    </script>
  </body>
  </html>
  ```

- [x] **1.3** Copy the color-modes IIFE verbatim from the `<head>` of
  `components/modules/web/www/index_ov2640.html` (the `(() => { 'use strict' ...
  })()` block) and paste it into the `<script>` tag in the OTA page head.

- [x] **1.4** Copy the theme-switcher SVG symbol block (`<svg class="d-none">
  <symbol id="check2"> ... </symbol> ... </svg>`) and the theme dropdown button
  from `index_ov2640.html` into the OTA page navbar.

---

## Task 2 — Write the JavaScript logic

- [x] **2.1** Implement file selection and drag-drop in the inline `<script>`:

  ```js
  const fileInput  = document.getElementById('file-input')
  const dropZone   = document.getElementById('drop-zone')
  const fileName   = document.getElementById('file-name')
  const flashBtn   = document.getElementById('flash-btn')
  let selectedFile = null

  function selectFile(f) {
    if (!f || !f.name.endsWith('.bin')) {
      showAlert('danger', 'Please select a valid .bin firmware file.')
      return
    }
    selectedFile = f
    fileName.textContent = `${f.name}  (${(f.size/1024/1024).toFixed(2)} MB)`
    flashBtn.disabled = false
  }

  fileInput.addEventListener('change', () => selectFile(fileInput.files[0]))
  dropZone.addEventListener('dragover',  e => { e.preventDefault(); dropZone.classList.add('drag-over') })
  dropZone.addEventListener('dragleave', () => dropZone.classList.remove('drag-over'))
  dropZone.addEventListener('drop', e => {
    e.preventDefault(); dropZone.classList.remove('drag-over')
    selectFile(e.dataTransfer.files[0])
  })
  ```

- [x] **2.2** Implement the upload XHR with Bootstrap progress bar:

  ```js
  flashBtn.addEventListener('click', () => {
    if (!selectedFile) return
    flashBtn.disabled = true
    document.getElementById('status-alert').classList.add('d-none')
    const progress = document.getElementById('upload-progress')
    const bar      = document.getElementById('upload-bar')
    const pctLabel = document.getElementById('pct-label')
    progress.classList.remove('d-none')

    const xhr = new XMLHttpRequest()
    xhr.open('POST', '/ota/upload')
    xhr.setRequestHeader('Content-Type', 'application/octet-stream')

    xhr.upload.onprogress = e => {
      if (!e.lengthComputable) return
      const pct = Math.round(e.loaded / e.total * 100)
      bar.style.width = pct + '%'
      pctLabel.textContent = `${pct}%  —  ${(e.loaded/1048576).toFixed(1)} / ${(e.total/1048576).toFixed(1)} MB`
    }

    xhr.onload = () => {
      progress.classList.add('d-none')
      flashBtn.disabled = false
      if (xhr.status === 200) {
        showAlert('success', '✓ Flash successful! Device is rebooting…')
        startCountdown(10)
      } else {
        showAlert('danger', 'Error: ' + xhr.responseText)
      }
    }

    xhr.onerror = () => {
      progress.classList.add('d-none')
      flashBtn.disabled = false
      showAlert('warning', 'Connection lost — device may already be rebooting.')
      startCountdown(12)
    }

    xhr.send(selectedFile)
  })
  ```

- [x] **2.3** Implement `showAlert()` and `startCountdown()`:

  ```js
  function showAlert(type, msg) {
    const el = document.getElementById('status-alert')
    el.className = `alert alert-${type} mt-3`
    el.innerHTML = msg
    el.classList.remove('d-none')
  }

  function startCountdown(seconds) {
    const section = document.getElementById('reboot-section')
    const cdSpan  = document.getElementById('cd')
    const cdBar   = document.getElementById('cd-bar')
    section.classList.remove('d-none')
    let remaining = seconds
    const tick = setInterval(() => {
      remaining--
      cdSpan.textContent = remaining
      cdBar.style.width  = Math.round(remaining / seconds * 100) + '%'
      if (remaining <= 0) { clearInterval(tick); location.href = '/' }
    }, 1000)
  }
  ```

---

## Task 3 — Encode the HTML as a C string and replace OTA_PAGE

- [x] **3.1** The HTML produced in Tasks 1–2 must be encoded as a C string
  literal. Rules:
  - Each logical line becomes a `"...\n"` string fragment.
  - Single quotes inside HTML attributes must stay as `'` (no escaping needed in
    C string — only `"` and `\` need escaping: use `\"` and `\\`).
  - The declaration must be:
    ```cpp
    static const char OTA_PAGE[] =
        "<!doctype html>..."
        "..."
        "...";
    ```
  - No `R"(...)"` raw string literals — they are not portable across all ESP-IDF
    gcc versions.

- [x] **3.2** Verify the string compiles cleanly:
  ```bash
  . ~/esp/esp-idf/export.sh
  idf.py build 2>&1 | grep -E "error:|OTA_PAGE"
  ```
  Expected: no errors, build succeeds.

---

## Task 4 — Smoke-test in browser

- [x] **4.1** Flash the firmware (`./deploy.sh` or `idf.py flash`).
- [x] **4.2** Connect to the ESP32 AP. Navigate to `http://192.168.4.1/update`.
- [x] **4.3** Confirm the page loads with Bootstrap styling (card, button, drop
  zone visible — not a plain white page with default fonts).
- [x] **4.4** Confirm the theme toggle dropdown appears and switching light/dark
  works.
- [x] **4.5** Confirm `← AI Cam S3` link in the navbar navigates to `/`.
