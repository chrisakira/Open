# Combat Log Implementation Plan

**Goal:** add the first slice of the bottom-left action chat: one truthful text entry per performed melee swing, including the actual dodge or block and damage that got through. Keep the overlay suitable for future action categories without implementing them now.

**Architecture:** `open::Character::try_melee_swing` records which defense actually ran on `SwingResult`. `CombatSession` already carries that result in its bounded performed-swing queue. `OpenCombat.take_combat_events` copies the defense into the event dictionary. `CombatFeedback` continues to drain that queue exactly once and emits each same event to the `CombatLog` HUD, in addition to its existing animation and damage-number work. Simulation is authoritative; presentation neither infers defense nor applies damage.

**Tech stack:** C++20 `open_sim`, CTest, GDExtension / godot-cpp `godot-4.5-stable`, Godot 4.7.2 GDScript and headless SceneTree tests.

**Spec:** `AGENTS.md`, `docs/combat_log.md` (locked bottom-left first slice), `docs/style.md`, `docs/combat.md` and `docs/weapons.md` (current defense). This file is a plan, not authorization to execute it.

## Global constraints

- Preserve pre-existing tracked edits and untracked work. Immediately before execution record `git status --short` and the relevant diffs; review final diffs against that baseline. At planning time `docs/README.md` was modified and `docs/combat_log.md`, the previous weapon-animation plan and `.superpowers/` were untracked. Do not access `.superpowers/`; no reset, checkout, stash, cleanup, staging or commit. Work sequentially, not in parallel in this tree. Only touch files named by the active task, preserving existing hunks.
- TDD: add focused failing tests before behavior changes. An expected missing-member build failure is interface RED, not an assertion RED; establish an executable failing assertion before writing the behavior. Do not weaken existing tests or silence errors.
- No gameplay changes: no altered combat clocks, defense selection, costs, wear, HP, XP, RNG, order or damage calculations. No Godot headers in `cpp/`. Log displays reports, not snapshots used to reconstruct a past outcome.
- No additional orders/movement/workbench/stamina events in this slice. No body-part or technique name, toggle, scroll, timed fade, new inventory content or gameplay debug setter. Later categories remain open in `docs/combat_log.md`.
- Preserve swing animation, floating numbers, held meshes and existing popup and world input behavior. `take_combat_events()` is destructive: exactly one production drain, and fan out that same event once per consumer. The existing session queue has a 64-event cap and drops oldest when full; preserve it and do not claim events dropped before draining can be shown.
- `SwingResult.damage` is the amount supplied to `Character::damage`, not necessarily HP lost after overkill clamps the target to zero. Display the event value, not a reconstructed HP delta.

## Lock / coverage register

| ID | Target | Tasks |
|---|---|---|
| L1 | Bottom-left dark panel, parchment text, click-through, no popup chrome | 3–4 |
| L2 | One entry per performed swing; no entry for a refused swing; event order | 1–4 |
| L3 | Defense names the actual attempted block/dodge, including zero prevention; no defense when cooling or stunned | 1–4 |
| L4 | Final positive event damage displayed, even after partial dodge or block; zero omitted; tiny positive is not `0` | 2–4 |
| L5 | Actor/target scene names (Player, Friendly, Enemy), not empty sheet names | 3–4 |
| L6 | Last eight event entries, newest at bottom; no timer or scrollbar | 3–4 |
| L7 | Same performed event still drives existing clips and damage labels; no second drain or damage application | 4 |
| L8 | Broader debug feed is future direction, not work shipped here | all |

A logical entry can wrap onto multiple visual lines. Keep eight entries, not eight wrapped screen rows.

## Live baseline inspected — not test results

- `SwingResult` (`cpp/include/open/combat_rules.hpp`) carries `damage`, `prevented`, `nullified`, optional `defense_item`, technique and attack cooldown. `DefendAction` already has `Block` and `Dodge` in `character_rules.hpp`.
- `Character::try_melee_swing` (`cpp/src/character_combat.cpp`) computes `defence_ready` from a real defense profile and its clock; a stunned defender has no profile. Ready defenses are attempted even for zero offense or zero mitigation. `defense_item` absent means either a dodge or no defense, so neither item ID, prevention nor nullification is a reliable defense classifier.
- `CombatSession` copies `SwingResult` into `CombatEvent`, preserves event order and bounds pending events at 64; its `take_swing_events()` drains. `OpenCombat.take_combat_events()` copies the reports into dictionaries and drains its bridge queue.
- `scripts/combat_feedback.gd::_physics_process` is the current production drain; `_play_reports` animates every event and makes a floating label for positive damage. `_damage_text` already avoids displaying tiny positive damage as zero. Existing tests call `_play_reports` directly with synthetic dictionaries; retain that supported fixture path.
- `scripts/main.gd::_ready` builds combat/feedback before the `CanvasLayer` HUD. The world steps at physics priority -10; feedback polls at default priority. Test the first real event after connection rather than assuming setup timing.
- `scripts/tests/` currently has 13 `*_test.gd` scripts; recount on execution. `cpp/CMakeLists.txt` has `weapon_combat`, `melee_swing`, `technique_combat` and `combat_session` CTest names. `project.godot` starts at 1280×720.

## Technical defaults (presentation only)

1. Add `std::optional<DefendAction> defense_action = std::nullopt` to `SwingResult`. Set it at result construction to `defence->action` iff `defence_ready`; otherwise leave empty. Use the already-computed event-time decision; never reselect after clocks/equipment change. A zero-prevention block is still a block. A paid full dodge and an exhausted partial dodge are both dodge. Refused attempts still produce no result/event.
2. Bridge dictionary key: `defense_action`, exact string values `"none"`, `"block"`, `"dodge"`. Keep all existing keys and meanings. Do not cast unvalidated arbitrary GDScript input into an enum; this value is emitted from a known C++ enum.
3. `CombatFeedback` emits `signal swing_reported(event: Dictionary)` exactly once per event inside `_play_reports`. Its `_physics_process` continues as sole production caller of `_combat.take_combat_events()`. Bind the UI before the first physics tick. Send the same event values to animations, floating labels and log without a second simulation call. Never mutate the shared event in a listener.
4. New `scripts/combat_log.gd` defines a presentation control with `bind_actors(actors: Array)` and `append_swing(event: Dictionary)`. Resolve IDs through valid actors' configured sheets (`get_character_id()`), then use the corresponding node `name`. An unknown or freed actor uses `Character <id>`, not a different character's name. Treat actor nodes as potentially freed; do not retain unguarded stale objects. Event data and actor names are plain text, not interpreted BBCode.
5. For a malformed or legacy synthetic event with absent/unknown `defense_action`, use the neutral no-defense text and optionally warn; never guess a defense. Production bridge events always carry the key. `damage` in a valid production event is finite and nonnegative. Each event is one entry even if no target actor is presently resolvable.
6. Sentence templates, Y = reported damage formatted as in existing floating labels:
   - none / Y > 0: `{attacker} attacks {target} and deals Y damage`
   - none / Y = 0: `{attacker} attacks {target}`
   - block or dodge / Y > 0: `{attacker} attacks but {target} blocks/dodges and receives Y damage`
   - block or dodge / Y = 0: `{attacker} attacks but {target} blocks/dodges`
   A zero-damage attempted defense can mean armor absorbed the rest; do not claim the defense itself prevented all damage. The neutral zero-damage wording and precise formatting reuse are presentation defaults; they are not new combat rules.
7. Extract the existing `_damage_text` algorithm unchanged into a small `scripts/combat_text.gd` static helper, preloaded by both feedback and log. Preserve `_damage_text` as a forwarding wrapper if existing tests/callers use it. No positive amount may print as zero. Do not change gameplay damage or floating label sizing.
8. Bottom-left `Control` on the existing HUD CanvasLayer: dark panel and readable parchment text from `docs/style.md`, no frame/title/close button. Eight logical entries retained in memory; drop oldest on entry nine. Wrap long text. Choose modest viewport-relative limits so the panel remains on-screen at 1280×720 and a smaller viewport, with no scroll or timer. Long names must not grow an unbounded panel; clip its display if necessary without discarding the eight logical entries. `mouse_filter = IGNORE` on the root and every child Control; do not add it to `_popup_covers`.

## File ownership and task dependencies

| Task | Depends on | Files it may edit |
|---|---|---|
| 1. Sim event | none | `cpp/include/open/combat_rules.hpp`, `cpp/src/character_combat.cpp`, `cpp/tests/weapon_combat_test.cpp`, `cpp/tests/melee_swing_test.cpp`, `cpp/tests/technique_combat_test.cpp`, `cpp/tests/combat_session_test.cpp` |
| 2. Bridge | 1 | `extension/combat_bridge.hpp`, `extension/combat_bridge.cpp`, `scripts/tests/combat_bridge_test.gd` |
| 3. Log control | 2 | new `scripts/combat_log.gd`, new `scripts/combat_text.gd`, `scripts/combat_feedback.gd` (formatter extraction only), new `scripts/tests/combat_log_test.gd`, `scripts/tests/combat_feedback_test.gd` (formatter regression only) |
| 4. Integration | 2, 3 | `scripts/combat_feedback.gd`, `scripts/main.gd`, `scripts/tests/combat_log_test.gd`, `scripts/tests/combat_feedback_test.gd`, `scripts/tests/melee_combat_test.gd` |
| 5. Verify / docs | 1–4 | `docs/combat_log.md` Current section only after implementation and verification; regression fixes in the files above if necessary |

No new extension class. Do not edit `cpp/src/combat_session.cpp` merely to carry the field: it already copies the `SwingResult` whole.

## Verification commands (execution only)

**SIM:**

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build -R '^(weapon_combat|melee_swing|technique_combat|combat_session)$' --output-on-failure
```

**EXT / IMPORT:** run after C++ or extension changes and before Godot tests. Use the pinned godot-cpp tag in CMake; do not point at a different local checkout.

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
```

**Focused Godot:**

```bash
godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd
godot --path . --headless --script res://scripts/tests/combat_log_test.gd
godot --path . --headless --script res://scripts/tests/combat_feedback_test.gd
godot --path . --headless --script res://scripts/tests/melee_combat_test.gd
```

Do not run the new log script before Task 3 creates it. Before each Godot test after a new `class_name` or extension change, import the current project first. Do not report commands in this plan as already run.

## Task 1 — Capture the attempted defense in C++

**Produces:** event-time `SwingResult.defense_action`: nullopt, `DefendAction::Block`, or `DefendAction::Dodge`. No rules change.

- [ ] RED: in real existing fixtures, assert the new result for: paid full dodge (`melee_swing_test.cpp`); exhausted half dodge (`weapon_combat_test.cpp`); partial shield block and zero-percent buckler (`weapon_combat_test.cpp`); full shield block (`melee_swing_test.cpp`); defense cooling (`weapon_combat_test.cpp`); ready defense against zero offense; and armor-zero following a real attempted defense (`technique_combat_test.cpp`). Check the independent damage/prevention results too, so `nullopt` is not guessed from those numbers.
- [ ] Add a real `CombatSession` event assertion: the copied value matches the swing, ordered events remain ordered, and the second drain is empty. Use the existing real-stun scenario in `combat_session_test.cpp` (the fight around its `a_stun_freezes_the_fighter` fixture) and inspect events from a step whose target remains stunned: assert a swing at that defender reports nullopt. Drain or isolate older fixture events first so event age is unambiguous; assert the relevant event exists. Do not inject stun through a new API.
- [ ] Preserve/refine existing refused out-of-range and attacker-cooldown cases: they produce no `SwingResult`, no session event, and do not change RNG/stamina/wear. Test a ready zero-offense attempt separately from refusal.
- [ ] Run SIM and observe the expected missing-member compile RED. Add only the default-initialized optional member; rebuild and observe executable assertion RED on actual dodge/block fixtures. Then assign from `defence_ready ? defence->action : std::nullopt` at result construction (express this in C++ with type-safe optional assignment). No new RNG calls or change to the defense branch.
- [ ] SIM GREEN. Run full `ctest --test-dir cpp/build --output-on-failure` before handing off. Check that unmodified damage, stamina, wear, XP and cooldown assertions remain green.

## Task 2 — Serialize the defense in GDExtension

**Produces:** every dictionary from `OpenCombat.take_combat_events()` includes `defense_action` as `none`, `block`, or `dodge`. Existing fields retain their values; bridge header key list also includes the already-existing `attack_cooldown`.

- [ ] First build EXT / IMPORT with Task 1, before changing the bridge. Add `combat_bridge_test.gd` assertions with guards so a missing key is an assertion failure rather than a script abort. A fresh unarmed target's first paid defense must report `dodge`.
- [ ] For `block`, configure and register an ordinary defender, call the real `setup_starter_player(id)`, find its owned `Shield_Round` in `get_equipment_snapshot()`, equip it in the left hand via `equip_item`, order the attack and step at close range. Assert the real event says `block`. Use separate fresh fixtures for `none`: two in-range attackers in one session frame against one target, first event consumes its ready defense, second event names `none` while that shared defense clock is cooling. The roster ordering must be explicit. No fabricated bridge event and no debug setter.
- [ ] Assert all events carry their old IDs/damage and the added key, event order is stable, and a second drain is empty. Check this test RED with old bridge. Add explicit mapping in `extension/combat_bridge.cpp` from the optional C++ enum to the three strings; document the key in the header. Do not infer from `defense_item` or `prevented`.
- [ ] EXT / IMPORT, then focused `combat_bridge_test.gd` GREEN. Re-run full CTest after extension build. Existing callers that ignore the added key continue to work.

## Task 3 — Build a testable log control and shared number formatting

**Produces:** `CombatLog.bind_actors(actors)` and `append_swing(event)`; a shared static damage formatter; eight-entry, click-through bottom-left control. No scene wiring yet.

- [ ] RED: create `scripts/tests/combat_log_test.gd` with guarded dynamic script loading/instantiation, so the absent control is a real failed assertion rather than an uncounted parse failure. Fixture actors may be normal test Nodes with real configured `OpenCharacter` sheets and distinct node names; synthetic dictionaries are presentation fixtures only, never simulation results. Use `checks`/case-completion guards like `combat_feedback_test.gd` so runtime errors do not silently pass.
- [ ] Assert exact template text for all six combinations of none/block/dodge × positive/zero damage, including partially mitigated block and dodge. Test zero-prevention block stays `blocks`, an armor-zero event stays truthful without claiming the reason, missing/unknown defense is neutral, and unknown/freed actor ID prints `Character <id>`. Include names containing BBCode-like characters as literal text.
- [ ] Assert a positive `0.001` and `0.00000001` are not displayed as `0`; compare exactly with the existing feedback formatter for several values including zero and a large value. Add a focused existing-feedback regression before extracting its current `_damage_text` logic unchanged to static `scripts/combat_text.gd` and preloading it from both users. Keep feedback's wrapper if tests call it.
- [ ] Assert ten distinct events retain exactly entries 3–10, in their original order, newest at bottom; a wrap is not a new event. Check no fade after time passes, root and all child Controls use `MOUSE_FILTER_IGNORE`, and log bounds/anchors are within both 1280×720 and a smaller viewport after a layout frame. Test long names without requiring a scrollbar. No log input can change gameplay.
- [ ] Implement the new control and helper to these interfaces; plain text, no rich-text markup. Place it in a temporary CanvasLayer in the focused test until Task 4 wires the scene. Run import, new `combat_log_test.gd` and existing `combat_feedback_test.gd` GREEN. Keep this task's `combat_feedback.gd` change limited to formatting; do not add event dispatch until Task 4.

## Task 4 — Fan out the real event and wire the HUD

**Produces:** one connected `CombatLog` on the existing HUD; one `swing_reported` emission per actual event consumed by feedback; existing animation and numbers unchanged.

- [ ] RED in `combat_feedback_test.gd` / `combat_log_test.gd`: a real world with an in-range order, stepped through the existing session and feedback path, sees the first performed swing in the log as well as one swing motion. Use the fresh target's paid dodge for zero damage and assert no floating number but one entry and one motion. A later live positive-damage event should yield a line, a motion and a floating label with the event's reported amount; use two attackers in one frame if needed to reach the already-spent defense as existing tests do. Capture via the signal/test listener, not by draining the queue a second time.
- [ ] Assert no extra line/motion on an out-of-range or attacker-cooling refusal; a later empty poll yields nothing. Feed multiple existing synthetic report dictionaries to `_play_reports` with explicit `defense_action` values and assert both ordered log entries and existing clip/damage behavior. For tests intended to cover legacy dictionaries, retain a deliberate missing-key case and neutral fallback. A log listener must not mutate the event seen by feedback.
- [ ] Add `signal swing_reported(event: Dictionary)` to `CombatFeedback`; emit once for each dictionary in `_play_reports`, while preserving the existing `_physics_process` drain and held-item updates. `main.gd::_build_hud` constructs a named `CombatLog` under the existing CanvasLayer, calls `bind_actors(_actors)`, and connects `_feedback.swing_reported` to `append_swing` during `_ready`, before the first physics callback. Do not connect twice after scene re-entry; no second `take_combat_events` anywhere.
- [ ] In `melee_combat_test.gd`, assert a floor click at a visible log-panel coordinate remains a world click: the click actually issues the expected movement order, not merely that `mouse_filter` is set. Verify normal popup input isolation remains intact. Also verify first-frame connection using the real scene and stable test-driven tick ordering, not a scene made solely of the control.
- [ ] Import, then all four focused Godot scripts GREEN. Confirm feedback's existing one-motion-per-performed-event and small-number tests are still present and passing.

## Task 5 — Full verification and current-state handoff

- [ ] Reconfigure ON, build, full CTest, headless import using EXT / IMPORT above. Check command exit statuses; do not claim a suite passed from a partial filter.
- [ ] Discover and run every current `scripts/tests/*_test.gd` from the project root, including the new one; fail immediately on a nonzero exit:

```bash
for test in scripts/tests/*_test.gd; do
  godot --path . --headless --script "res://$test" || exit 1
done
```

Record the discovered script count and actual results; preserve existing tests.
- [ ] Desktop visual pass, separate from headless: at 1280×720 and a smaller viewport, inspect bottom-left placement, text contrast/wrapping and last-eight behavior; exercise a real dodge, real positive hit and shield block; verify small damage is legible and clicking through the panel reaches the world. Check character/equipment popups, movement, swing clips and floating labels. If no desktop is available, report this as not run, not as a pass.
- [ ] Only after implementation/tests, update `docs/combat_log.md` Current paragraph to state what really exists and that `CombatFeedback` shares its one drain with the log. Retain the locked first slice and future/open categories. Do not mark a visual pass completed unless it was actually observed.
- [ ] Review all changed files against the execution-time baseline. Report C++ suite, focused and full Godot suites, desktop result and any limitation separately. No reset, staging or commit.

## Acceptance matrix

| Evidence | Required check |
|---|---|
| Authoritative event | C++ optional action: full/exhausted dodge, full/partial/zero block, cooling/stunned none, zero offense, refused swing |
| Bridge | Real events serialize `none` / `block` / `dodge`; old keys and drain-once retained |
| Formatting | Six outcome templates, zero and tiny positive, unknown actor, literal names, no inferred defense |
| History and input | Eight logical entries in order, wrapping/resize, no timed disappearance, actual world click passes through |
| End-to-end | First real event reaches animation and log; positive real hit additionally reaches floating damage; zero and refusal correctly distinguished |
| Regressions | Full CTest plus every headless Godot test after fresh extension and import; separate desktop visual pass |
