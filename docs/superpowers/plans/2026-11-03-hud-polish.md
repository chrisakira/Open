# HUD Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring the existing HUD closer to the approved mockup: compact overhead plaques, a readable bottom-centred player panel, a separate bottom-left log and a restrained top hint strip.

**Architecture:** Retain the scene-owned `StatusOverlay`, shared `StatusBar`, and existing combat log; change presentation and layout only. Fix the shared bar's minimum sizing so world bars and player rows actually fit their intended geometry. Use the approved mockup as visual direction, not as a source of new controls or events.

**Tech Stack:** Godot 4.7, GDScript, existing GDExtension-backed character views.

**Spec:** User-approved mockup in this conversation; existing `docs/style.md`, `docs/status_UID.md`, and `docs/combat_log.md` govern unchanged behavior. The approved mockup supersedes previous status geometry and chrome choices where they conflict.

## Global Constraints

- Preserve existing uncommitted and untracked work; do not replace status implementation or unrelated edits.
- Presentation only: no C++ changes, new gameplay controls, fictional combat events, tabs, or simulation state.
- Preserve actual hint text and existing controls, live per-pool values and two-decimal formatting, faction diamond, death skull, click-through, overlay ownership and visibility gates.
- Use rustic dark wood, muted brass and parchment styling from `docs/style.md`; the mockup's characters and environment are illustrative.
- Preserve bottom-centred player and bottom-left log; keep them separate at 1280×720 and 720×420 when practical.

## Review Focus

- Shared bar minimum sizes must not expand a 120-pixel world stack or overflow the player panel (Task 1).
- Long names and three-digit/two-decimal values must stay within their surfaces (Task 1).
- A small viewport must keep both the player panel and log on screen and apart (Task 2).
- Resize, death, faction, changes to pool values, and actor projection must preserve existing behavior (Tasks 1–2).
- Hint and decorative chrome must not intercept floor clicks or advertise unavailable controls (Task 2).

---

### Task 1: Size and style status plaques

**Files:**
- Modify: `scripts/status_bar.gd`, `scripts/player_status.gd`, `scripts/world_status.gd`
- Test: `scripts/tests/player_status_test.gd`, `scripts/tests/world_status_test.gd`, `scripts/tests/status_integration_test.gd`
- Modify: `docs/status_UID.md`

**Interfaces:** Retain `present(current: float, maximum: float, text: String)`, `bind_sheet`, `bind_actor`, `refresh`, and existing named child paths. `StatusOverlay` continues positioning world views above actors.

- [ ] **Step 1: Add failing geometry assertions.** In the player status test, after two frames assert the plaque contains every row and each role/value label; check the 20-character display name and `100.00 / 100.00` fit without overlapping the bar. In the world status test, assert each bar's actual width is at most 120 pixels, all three fit within a compact framed plaque, and the name, level, diamond and optional skull fit within that plaque. Assert click-through remains recursive.
- [ ] **Step 2: Run the player and world tests.** Run `godot --path . --headless --script scripts/tests/player_status_test.gd` and `godot --path . --headless --script scripts/tests/world_status_test.gd`; confirm the new geometry checks fail for the existing implementation rather than a missing extension.
- [ ] **Step 3: Implement the layouts.** Remove the shared row's 340-pixel hard minimum and collapse hidden world label/value controls; align player text, tracks and readouts inside a bottom-centred wood-and-brass panel with 16-pixel bottom margin. Add a compact framed world plaque with name/relationship/death identity, level and three narrow bars, using the existing colors and live data. Set explicit serif font sizes and account for actual font measurements and long names. Keep the existing child paths and simulation-facing methods.
- [ ] **Step 4: Verify.** Run the two tests above and `godot --path . --headless --script scripts/tests/status_integration_test.gd`; all must pass. Check resized and current/max changing cases in the existing tests.
- [ ] **Step 5: Update `docs/status_UID.md`.** Record the newly approved visual geometry while preserving the documented simulation and visibility contracts, including deferred wall occlusion.

### Task 2: Separate the log and style the hint

**Files:**
- Modify: `scripts/combat_log.gd`, `scripts/main.gd`
- Test: `scripts/tests/combat_log_test.gd`, `scripts/tests/status_integration_test.gd`
- Modify: `docs/combat_log.md` if its current chrome description changes.

**Interfaces:** Retain `CombatLog.bind_actors(actors: Array)` and `append_swing(event: Dictionary)`; hint remains presentation-only and reports the existing click, I and P controls accurately.

- [ ] **Step 1: Add failing layout checks.** Instantiate the real scene at 1280×720 and 720×420, wait for layout, and assert the player panel and log stay within the viewport and do not intersect. Assert the hint retains the real control descriptions and the hint and log ignore pointer input. Preserve existing log history, wrapping and event wording tests.
- [ ] **Step 2: Run the relevant tests.** Run `godot --path . --headless --script scripts/tests/status_integration_test.gd` and `godot --path . --headless --script scripts/tests/combat_log_test.gd`; confirm the new checks expose the existing layout or hint styling gap.
- [ ] **Step 3: Implement presentation.** Make the log compact enough to remain left of the player panel at the tested viewport sizes while retaining its eight-event cap, wrapping, clipping and click-through behavior. Present the existing help text in a slim centred dark/brass strip; do not add the mockup's invented key bindings or tabs.
- [ ] **Step 4: Verify.** Re-run both tests and the two status tests. Run `godot --path . --headless --import --quit` before the suite if script imports or class signatures changed.
- [ ] **Step 5: Visual verification.** Launch the actual scene at normal and smaller sizes and inspect the HUD against the mockup: no clipped labels, oversized bars, overlaps, or misleading controls. If a graphical session is unavailable, explicitly report that visual verification was not performed.
