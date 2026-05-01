# GitHub Copilot Instructions — AICamRobot

This file is automatically loaded by VS Code / GitHub Copilot Chat as workspace context.

---

## Project summary

ESP32-S3 AI camera firmware (ESP-IDF v5.5+). FreeRTOS pipeline:
`Camera → xQueueAIFrame → AI detection → xQueueHttpFrame → HTTP MJPEG streamer`

- **Target board:** ESP32-S3-WROOM-CAM, 16 MB flash, Octal PSRAM
- **Build system:** ESP-IDF CMake (`idf.py build`)
- **AI library:** `components/esp-dl/` (submodule, do not edit)
- **Web server:** `components/modules/web/app_httpd.cpp`
- **Web UI:** `components/modules/web/www/index_ov2640.html` (gzip-embedded)
- **Partition table:** `partitions.csv` (OTA layout: nvs + otadata + ota_0 + ota_1 + fr)

Key rules:
- Source ESP-IDF before any build: `. ~/esp/esp-idf/export.sh`
- New `.c`/`.cpp` files → update `SRC_DIRS` or `SRCS` in the relevant `CMakeLists.txt`
- New HTTP URI handlers → increment `max_uri_handlers` in `register_httpd()`
- HTML changes → regenerate `.gz`: `gzip -9 -f -k index_ov2640.html`
- Never edit files under `managed_components/` or `components/esp-dl/`

---

## Receiving tasks from Claude

Claude (the Planner) creates structured plan folders under `.plans/`. You (Copilot, the Executor) implement them.

### How to find a plan

When the user says something like:

> "Implement the plan from `.plans/2025-05-01-ota-update/`"

Go to that folder and read these files **in order**:

1. `spec.md` — what to build and why (read this first for context)
2. `tasks.md` — ordered list of implementation steps
3. `tests.md` — acceptance criteria / test commands for each task

### How to implement

1. Read all three files completely before writing any code.
2. Implement tasks strictly in the order listed in `tasks.md`.
3. After completing each task, run (or describe) the corresponding test from `tests.md`.
4. Mark the task `[x]` in `tasks.md` when its test passes.
5. **If a test fails:** stop immediately. Do not continue to the next task.
   Report the exact error message and which task/test failed.
6. After all tasks are done, output a short summary:
   - What was implemented
   - Which tests passed
   - Any issues or deviations from the plan

### If direct folder reading is not available

If you cannot browse the workspace (e.g., in a browser-based Copilot session), the user will paste the plan content directly. Look for a message starting with:

```
--- PLAN: spec.md ---
...
--- PLAN: tasks.md ---
...
--- PLAN: tests.md ---
```

Treat that pasted content as the authoritative plan and proceed normally.

### What NOT to do

- Do not refactor code outside the scope of the current task.
- Do not add features not listed in `tasks.md`.
- Do not modify `managed_components/` or `components/esp-dl/`.
- Do not skip a failing test and continue to the next task.

---

## Code style

- C/C++: follow existing style in `app_httpd.cpp` (K&R braces, `ESP_LOGI` for logging)
- No new comments unless the logic is non-obvious
- No error handling for impossible cases (trust ESP-IDF guarantees)
- Prefer editing existing files over creating new ones
