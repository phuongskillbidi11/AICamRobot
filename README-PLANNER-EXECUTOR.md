# Planner (Claude) + Executor (Copilot) Workflow

Claude thinks and plans. Copilot reads and implements.
They do not talk to each other — you are the relay.

---

## Step 1 — Ask Claude to create a plan

In Claude Code (this terminal), say:

```
Claude, create a plan for <feature>.
Save it to .plans/YYYY-MM-DD-<feature-name>/
Include spec.md, tasks.md, and tests.md.
```

Example:
```
Claude, create a plan for adding OTA firmware update via the web UI.
Save it to .plans/2025-05-01-ota-update/
```

Claude will write three files:
- `spec.md` — goals, constraints, design decisions
- `tasks.md` — ordered checklist of implementation steps
- `tests.md` — how to verify each task (build commands, serial output, HTTP responses)

---

## Step 2 — Ask Copilot to implement the plan

Open VS Code → Copilot Chat. Type:

```
Implement the plan from .plans/2025-05-01-ota-update/
Read spec.md, tasks.md, and tests.md from that folder, then implement the tasks in order.
After each task, check the test. Mark [x] when it passes.
Stop and report if a test fails.
```

Copilot will read the plan files from your workspace and implement each task.

### If Copilot cannot read workspace files

Some Copilot environments (browser, early VS Code versions) cannot browse folders.
In that case, paste the plan manually using this template:

```
--- PLAN: spec.md ---
<paste contents of .plans/2025-05-01-ota-update/spec.md here>

--- PLAN: tasks.md ---
<paste contents of .plans/2025-05-01-ota-update/tasks.md here>

--- PLAN: tests.md ---
<paste contents of .plans/2025-05-01-ota-update/tests.md here>
---

Implement the tasks above in order.
After each task, check the corresponding test.
Mark [x] in tasks.md when the test passes.
Stop and report the exact error if a test fails.
```

You can also drag the three `.md` files into the VS Code Copilot Chat input area — they will be attached as context automatically.

---

## Step 3 — Report failures back to Claude

If Copilot stops on a failing test, bring the error back to Claude:

```
Claude, Copilot failed on task 3 of .plans/2025-05-01-ota-update/
Error: <paste exact error>
Context: <what Copilot tried>
Please update tasks.md with a corrected approach for that task.
```

Claude will revise only the failing task. You then give Copilot the updated `tasks.md` and resume from that task.

---

## Plan folder conventions

```
.plans/
└── YYYY-MM-DD-feature-name/
    ├── spec.md      # What to build and why
    ├── tasks.md     # Ordered [ ] checklist — Copilot marks [x] as it goes
    └── tests.md     # Acceptance criteria per task
```

Completed plans stay in `.plans/` as a record. Do not delete them.

---

## Quick reference card

| You want to…                  | Say to…  | Message                                      |
|-------------------------------|----------|----------------------------------------------|
| Create a new plan             | Claude   | "Create a plan for X, save to .plans/…"     |
| Implement a plan              | Copilot  | "Implement the plan from .plans/…"           |
| Fix a failing task            | Claude   | "Copilot failed on task N with error: …"    |
| Resume after fix              | Copilot  | "Resume from task N with updated tasks.md"  |
| Review what was built         | Claude   | "Review the diff against the plan in .plans/…" |
