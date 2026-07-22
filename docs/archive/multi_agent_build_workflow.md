# Multi-Agent Build & Review Workflow
### Background Execution Migration — for Antigravity (Gemini)

## Note to the orchestrating agent reading this

**Your job in this workflow is to spawn, sequence, and route work between
subagents — not to write the code yourself.** If you find yourself about to
write or edit a file directly, stop: that means a Coder subagent should be doing
it instead. Concretely, for every task below:

1. Spawn a fresh subagent with the exact prompt template given for that role.
2. Wait for it to finish and report back (file diff, or review verdict).
3. Do not silently "fix" or "improve" a reviewer's findings yourself — route
   the fix back to the original Coder subagent as a new instruction, then
   re-run review on the result.
4. Do not let one subagent session play multiple roles (e.g. the same agent
   that coded a file must not also review it, even in a "second pass"). Spawn
   genuinely separate sessions for Coder / Task Reviewer / File Reviewer /
   Global Reviewer, per the tiers below.
5. If a review comes back FAIL, the loop is: Coder fixes → same two reviewers
   re-check the specific failed items only (not a full re-review from scratch,
   unless the fix was large) → proceed once both PASS.
6. Keep a running log (in a scratch file or your own summary) of which
   tier/file/task is done, in-review, or blocked — you are the only thing
   holding the overall sequence together across all these separate subagent
   sessions.

This is not a "one coder per file, done in parallel" workflow, even though that was
the original idea — the files in this plan aren't independent. `ble_service.dart`
literally has to guess the exact command names and payload shapes that
`background_engine.dart` invents, unless something pins those down first. Antigravity
agents working in separate workspaces **do not see each other's file changes**
unless you route them through something shared. So step zero is building that
shared thing — a **Contract Doc** — before any file-level coder agent starts.

Everything below is built around: **Contract → Tiered coding → Task-level review →
File-level review → Global integration review.**

---

## Step 0 — Build the Contract Doc first (you + one agent, not skipped)

Create `docs/isolate_contract.md` in the repo. This is the single source of truth
every subsequent agent reads before writing a line of code. It must pin down,
concretely (not "TBD"):

- **Every command name** UI→background can send (`connect_ble`, `update_role`,
  `request_full_state`, `stop_engine`, etc.) and the exact shape of each payload.
- **Every message name** background→UI can send (`fullState`, `session_expired`,
  token-rotation payload, `syncError`, BLE/GPS live updates) and exact payload shape.
- **SharedPreferences keys** used by both isolates (`ble_mac_address`, access token
  key, refresh token key) — exact string literals, not descriptions.
- **The teardown sequencing rule**: `session_expired`/state message must be sent
  and acknowledged/flushed *before* `stopService()` is called.
- **The boot gate rule**: engine checks for a valid token before doing anything
  else on boot; if absent, it must call `stopService()` immediately.
- **The ring buffer rule**: cap (100 items), eviction (drop oldest), flush order
  (chronological), flush pacing (throttled to normal polling interval, not burst).
- **The 401 UI-side wait rule**: UI interceptor waits on *either* token-update *or*
  `session_expired` — both must resolve the wait, not just one.

Have one agent draft this from `implementation_plan.md` + `task.md`, then **you
personally read and edit it** before moving on. Every downstream agent's prompt
will include: *"Read `docs/isolate_contract.md` in full before writing any code.
If anything you need isn't specified there, stop and ask — do not invent it."*

This single step removes 80% of the hallucination risk you're worried about,
because it turns "does this code make sense in isolation" (easy to fake-pass)
into "does this code match the contract" (checkable by grep, not vibes).

---

## Step 1 — Tiered build order (respects real dependencies)

Do not start all files at once. Antigravity's Manager Surface lets you queue
these, but keep the tiers sequential — a later tier's coder agent should only
start once the earlier tier is reviewed and merged.

**Tier A — Foundation (no dependents' code needed yet)**
- `AndroidManifest.xml` changes (stopWithTask, foregroundServiceType, boot receiver/permission)
- `pubspec.yaml` update

**Tier B — Core engine + protocol (depends on Contract Doc only)**
- `background_engine.dart` (new file)
- `foreground_service.dart` (`_ForegroundTaskHandler` redesign)

**Tier C — UI-side consumers (depend on Tier B's actual committed command/message names)**
- `ble_service.dart`, `location_service.dart` (thin proxies)
- `main.dart`, `home_screen.dart` (permission gating, wakelock config, 401 interceptor, token passing)
- `sync_service.dart` if touched

Each tier gets fully coded, task-reviewed, file-reviewed, and merged before the
next tier's coder agent is spawned — because Tier C agents need to read the
*actual* command strings Tier B produced, not the contract's draft of them (the
contract is the plan; Tier B's code is the ground truth once written — if they
diverge, that's itself a review finding).

---

## Step 2 — Roles (spawn each as a separate Antigravity agent/task)

### Role: Coder (one per file, per tier)
Prompt template:
> You are implementing exactly one file: `{filepath}`. Read `docs/isolate_contract.md`
> in full first. Read `implementation_plan.md` and the relevant bullet(s) in
> `task.md` for this file. Do not invent command names, payload shapes, or
> SharedPreferences keys not in the contract doc — if something is missing, stop
> and list what's missing instead of guessing. When done, list every command/message
> name and every SharedPreferences key your code uses, so reviewers can check them
> against the contract without re-reading your whole diff.

### Role: Task Reviewers (2 independent agents, run after each coder finishes)
These check **one task-list checkbox at a time**, not the whole file. Give both
reviewers the *same* checklist (below) but do not let them see each other's
output before submitting — that's the point of having two.

### Role: File Reviewers (2 independent agents, run after all tasks for a file pass)
These read the **entire finished file** in one pass, checking for things that
only show up at whole-file scope: consistent error handling, no dead code paths,
no contradictory logic between two tasks that each individually looked fine.

### Role: Global Reviewer (1 agent, after all tiers merged)
Checks cross-file consistency against the contract doc and the specific
integration risks listed in Step 4. This one has read access to all changed
files simultaneously — it's the only role for which that's true.

**Review output format, enforced for every reviewer:**
```
VERDICT: PASS | FAIL
For each checklist item: PASS/FAIL + file:line citation + one-sentence reason.
Do not approve anything you did not personally trace through the code.
If FAIL: exact fix required, not a vague concern.
```
Reject any review response that says "looks good" without line citations —
that's the rubber-stamp failure mode you're trying to avoid.

---

## Step 3 — Per-checkbox reviewer checklists

Use these verbatim in reviewer prompts — they're the specific things four rounds
of plan review surfaced, and they're exactly what a generic "review this code"
prompt will *not* reliably catch on its own.

**`background_engine.dart` — Boot Self-Hydration task**
- [ ] Does it check for a valid (non-empty, non-expired-looking) token *before*
      touching BLE/GPS/network at all?
- [ ] If token check fails, does it call `stopService()` and return, doing
      nothing else?
- [ ] Does it also read `ble_mac_address` only *after* the token check passes?

**`background_engine.dart` — Telemetry Ring Buffer task**
- [ ] Is there an explicit cap (~100) enforced on insert, not just "intended"?
- [ ] Does exceeding the cap drop the *oldest* item, not throw or silently grow?
- [ ] On flush, are items sent in original chronological order?
- [ ] Is the flush throttled (delay between sends) rather than a tight loop?

**`background_engine.dart` — 401/Token Rotation task**
- [ ] On successful refresh: is the new token saved to SharedPreferences
      *and* piped to the UI isolate — both, not one?
- [ ] On permanent failure: is `session_expired` sent over the port, then
      (only after) is `stop_engine`/teardown invoked — check the actual order
      of statements/awaits, not just that both exist somewhere in the function?
- [ ] Is there any code path where the UI isolate's `SupabaseClient` could
      also attempt a refresh? (There shouldn't be one — confirm
      `autoRefreshToken: false` is set on the UI-side client in the other file.)

**`foreground_service.dart` — Command queue / handshake task**
- [ ] Are commands received before init-complete actually queued (stored),
      not dropped?
- [ ] Are queued commands replayed in order once init completes?
- [ ] Does `request_full_state` get answered correctly both on cold start
      (via queue) and warm reattach (immediately)?

**`main.dart` / `home_screen.dart` — 401 UI interceptor task**
- [ ] Does the wait resolve on *either* a token update *or* a `session_expired`
      message — two exit conditions, not one?
- [ ] Is there a timeout as a last-resort escape hatch in case neither message
      ever arrives (e.g. port dies mid-teardown)?
- [ ] Is `autoRefreshToken: false` actually set on this client's init options?

**`AndroidManifest.xml` task**
- [ ] `stopWithTask="false"` present on the correct `<service>` element?
- [ ] `foregroundServiceType` includes both `location` and `connectedDevice`?
- [ ] `RECEIVE_BOOT_COMPLETED` permission present, and a `<receiver>` entry
      matching what `flutter_foreground_task`'s `autoRunOnBoot` expects for the
      installed plugin version (check the plugin's own docs/example manifest —
      don't assume from memory, plugin versions differ)?

---

## Step 4 — Global Reviewer's cross-file checklist

Only this reviewer sees everything at once — this is where integration bugs
that no single-file review can catch get caught.

- [ ] Command/message names used in `ble_service.dart`/`location_service.dart`
      exactly match (string-for-string) what `background_engine.dart` and
      `foreground_service.dart` actually emit/expect — grep both sides.
- [ ] SharedPreferences keys are identical string literals everywhere they're
      used across all files.
- [ ] The teardown-then-session_expired ordering is consistent with what the
      UI-side interceptor actually listens for.
- [ ] No file still contains a `supabase_flutter`-dependent call
      (`Supabase.instance.client...`) inside anything that runs in the
      background isolate.
- [ ] Run `flutter analyze` and paste the actual output — not a description of
      having run it — as evidence in the artifact.

---

## Step 5 — Manual verification (still required, no agent replaces this)

These need a real device and cannot be marked PASS by any reviewer agent based
on code reading alone — flag any reviewer that claims otherwise as a hallucination:

1. Aggressive OEM device (Xiaomi/Samsung) swipe-away test
2. Reboot-with-screen-off test, confirming FGS actually starts (not just receiver firing)
3. Force Stop test (expect: stays dead)
4. Session-expiry test: confirm no ghost foreground-service notification after
   a logout, even across a reboot

Attach screen recordings or logcat excerpts as Antigravity Artifacts for these —
don't accept a reviewer agent's text claim that a manual step passed.

---

## Practical notes for running this in Antigravity

- Use the **Manager Surface** to queue Tier A → B → C, one tier fully merged
  before spawning the next tier's coder(s) — don't fire all coders at once,
  since Tier C's correctness depends on Tier B's actual output.
- Give every agent (coder and reviewer) the same repo state and the same
  `docs/isolate_contract.md` — if a reviewer's tool-call output shows it looking
  at a stale file version, re-run it; Antigravity agents in separate
  sessions can silently drift on file state.
- Don't let a coder review its own file, even across separate sessions of the
  "same" agent identity — spawn genuinely separate review tasks.
- If a Task or File reviewer disagrees with a peer reviewer, don't average
  it out — resolve it explicitly (usually by re-reading the disputed
  file:line yourself) rather than letting a 1-1 split silently pass.
