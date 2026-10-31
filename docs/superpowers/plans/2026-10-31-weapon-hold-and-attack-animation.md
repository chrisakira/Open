# Weapon Hold and Attack Animation Implementation Plan

**Goal:** replace floating held meshes and the whole-body Visual tilt with bone-parented grips and event-driven skeletal attack clips, matching the locked presentation in `docs/weapons.md`. Combat math, reach, clocks and orders stay C++.

**Architecture:** keep C++ as the authority for whether a swing happened and how long that swing's attack cooldown is. GDExtension copies that cooldown onto the existing combat event. Godot parents meshes to `hand_r` / `hand_l`, selects idle from equipped state, and plays retargeted Quaternius clips. Presentation does not apply damage, start clocks, or invent swings.

**Tech Stack:** C++20 / `open_sim`, CTest, Godot 4.7.2, GDExtension / godot-cpp `godot-4.5-stable`, SceneTree GDScript tests.

**Spec:** `AGENTS.md`, `docs/weapons.md` (Starter combat presentation — Current vs Locked hand bones / attack clips / polish boundary), `docs/combat.md` (orders and clocks). The 2026-10-29 weapons plan is historical for this slice; do not reopen its gameplay tasks.

This file is planning only, not authorization to implement or to launch OpenCode.

## Global constraints

- Preserve all pre-existing dirty tracked and untracked work; record `git status --short` and relevant diffs immediately before execution because the workspace may change concurrently. At initial review `character_combat.cpp`, `character_work.cpp`, `combat_session.cpp`, `melee_swing_test.cpp`, `work_actions_test.cpp`, and `docs/weapons.md` were dirty; the C++ changes were subsequently committed externally during review. No reset, stash, checkout, cleanup, staging, commits, or `.superpowers/` access. Only edit the file list for each task, preserving any pre-existing hunks. `character_combat.cpp` may receive only the narrow cooldown-result assignment (Task 1); `docs/weapons.md` only the presentation Current/summary updates (Task 4). `SwingResult` already rides inside `CombatEvent`, so adding a field does not require `combat_session.cpp`. Compare final diffs against the execution-time baseline, not this historical list. Do not work concurrently in the same tree.
- Open is not Project_OPEN. No new lore, cameras, dual-wield, ranged/magic, block/dodge/stun/death clips, spear two-hand IK, or new animation packs.
- `cpp/` must not include Godot headers. GDScript must not own damage, cooldown, reach, or whether a swing occurred.
- Preserve idle/walk retargeting, hair-on-`Head`, scenery weapon inspection, equipment popup, technique popup, damage labels, event drain-once, and `swing_motion_count` as the motion counter tests already use.
- Left-hand weapons follow `hand_l` as locked. Attack clips in the library are right-handed; do not mirror them or reparent a left-hand item onto `hand_r` during a swing.
- Exact grip numbers are presentation tuning, revisitable. Do not treat them as combat reach.
- TDD: failing test before each sim or Godot behavior change. Never weaken tests. Headless is not desktop.

## Lock register

| Contract | Locked scope | Tasks |
|---|---|---|
| H1 bones | Equipped weapon/shield meshes parent to `BoneAttachment3D` on `hand_r` / `hand_l` through idle, walk and attack. One-hand follows C++ slot. Two-hand (spear) is one mesh on `hand_r`. Disabled arms keep meshes; no animation for a refused swing, but performed fallback attacks still animate. Side-specific one-hander behavior is gated below. | 2–3 |
| H2 grip | Per-starter local offset/rotation/scale so the handle sits in the palm. Presentation only. Existing OBJ models, no new meshes. | 2 |
| H3 equip | Instant attach/detach on the next presentation tick. No draw/sheathe. Scenery stays scenery. | 2 |
| H4 idle | Usable weapon: `Sword_Idle`. Shield only: `Idle_Shield` (imported name may drop `_Loop`). Walk stays `Walk`. | 3 |
| H5 clips | Performed `take_combat_events` swing plays one skeletal clip, including blocked / armour-zero / zero-damage. Refused attempt: none. Replace Visual tilt. C++ owns the attack clock. | 1, 3–4 |
| H6 map | Basic and Slash (1): `Sword_Regular_A`. Thrust (2): `Sword_Regular_B`. Heavy Strike (3): `Sword_Heavy_Combo`. Unarmed: `Punch_Jab`. Same map for sword, dagger, spear, hammer. `technique_id` 0 is basic. | 3–4 |
| H7 scale | Playback duration equals that swing's C++ attack cooldown, including exhausted doubling. | 1, 3–4 |
| H8 numbers | Damage labels stay on the event. No sim wind-up. | 4 |
| H9 boundary | No block, dodge, hit-react, stun, death, spear IK, or new packs. | all |

## Decision gate: disabled one-handed weapon

The locked presentation says a disabled holding arm suppresses attack motion, but live `cpp/src/equipment.cpp::Equipment::held_usable` checks only whether at least one arm survives for any one-hander; it does not check the arm corresponding to the equipped slot. Thus a sword in a disabled right arm can still generate a right-hand weapon swing if the left arm survives. Silently suppressing that event's animation would also violate the lock to animate every performed swing. Fixing this in C++ changes attack and defense selection, not just presentation. Do not guess or hide the event: obtain the user's decision whether this plan should include the gameplay fix or preserve current gameplay and defer the side-specific lock. Until decided, the overall plan is not fully implementation-ready; independent animation and bridge tasks can proceed without claiming this case is solved.

## Technical defaults (not new gameplay)

1. **Cooldown on the event.** Add `float attack_cooldown` to `SwingResult`, set in `Character::try_melee_swing` to the same `weapon_cooldown(...)` value written to `attack_cooldown_`. Copy it through `OpenCombat.take_combat_events` as `attack_cooldown`. Do not have Godot read `attack_cooldown_remaining()` later: a delayed drain would scale to a partially ticked clock. Nonfinite or `<= 0` cooldown plays the clip at speed 1.
2. **Socket names stay.** Create `BoneAttachment3D` nodes named `LeftHandSocket` / `RightHandSocket` as children of the actor's `Skeleton3D` (`bone_name` `hand_l` / `hand_r`). Existing tests walk parents looking for those names. Do not parent sockets to the `Visual` wrapper.
3. **One AnimationPlayer.** Keep the pose script's player. Load the extra clips with the same retarget path as idle/walk. Resolve imported names by trying the locked name, then the `_Loop` suffix. Actual imports: `Idle_Shield` and the attack clips are in UAL2; `Sword_Idle` and `Punch_Jab` are in UAL1. `Sword_Idle` imports as non-looping: set its duplicated retargeted Animation's loop mode to linear, without changing shared imported assets. Attack clips remain non-looping. No AnimationTree, no lower-body walk layered under the swing.
4. **Walk yields to attack.** While an attack is active, `update_walk` saves the latest speed fraction but neither changes the current animation nor overwrites its attack `speed_scale`. On clip finish clear the attack flag, restore locomotion speed, and select the latest walk/idle. Explicitly restart from time 0 even when two successive events select the same clip. Each new event immediately replaces the previous clip and duration. Missing clips warn, clear attack state and resume locomotion. Restore `speed_scale` when leaving an attack. Equip changes during an attack update the pending idle without interrupting the attack. Frame updates and finish handlers are presentation only, never combat clocks.
5. **Grip table.** Presentation constants on `CombatFeedback` (or a small script-local dict) keyed by starter `model_key`: `Sword`, `Dagger`, `Spear`, `Hammer_Small`, `Shield_Round`. Start from today's `MODEL_SCALE` 0.22 and `rotation_degrees.x = 90`, then offset so the handle sits in the palm. Unknown keys: scale 0.22, no extra offset, still attach.
6. **Armed idle input needs a real bridge.** Current equipment rows contain neither item type nor limb usability; `get_weapon_snapshot(id)` describes owned weapons even when carried or unusable. Extend each `OpenCharacter.get_equipment_snapshot()` row with boolean `is_weapon`, `is_shield`, and `held_usable`, computed in C++ from the catalog definition and `Character::held_usable(item_id)` (false for non-held items); add the fields to `EquipmentSnapshotRow` and the existing bridge dictionary. No gameplay setters. On every feedback tick, compute idle separately from mesh signature: any held usable weapon → `weapon`; otherwise any equipped shield → `shield`; otherwise `unarmed`. This matches the locked shield-only fallback without inventing shield usability. Pass the result to the pose's `set_armed_idle(kind)` even when equipment IDs have not changed: arm damage can change usability without changing slots. Model selection for an actual swing uses the event's `attacker_item`, not current equipment.
7. **Motion counter.** Keep incrementing `swing_motions` once per performed event, even if the named clip is missing (warn). Tests already assert the counter, not a tween angle.
8. **Slot/identity synchronization.** Existing `_sync_attachments` keys only by `model_key`, so moving an item between hands before the next tick or holding two same-model instances can leave the wrong mesh. Match by item ID and slot, remove/reparent stale entries, and make one mesh per distinct equipped instance (a two-hander remains one). Test same-model instances if produced through the real crafting path; at minimum test unequip/re-equip to the other hand within one tick.

## Live baseline inspected (not test results)

- `CombatFeedback` parents `Held_*` meshes to `Visual/LeftHandSocket` and `Visual/RightHandSocket` at hardcoded offsets `(-/+0.34, 1.06, -0.16)`, rotation X 90, scale 0.22. `_play_swing` tweens `Visual.rotation_degrees.x` by -34° over 0.28s.
- `character_model_pose.gd` retargets only the exported idle and walk from the two Quaternius GLBs onto the Superhero skeleton. Hair uses `BoneAttachment3D` on `Head`. `player.gd` / `standing_dummy.gd` call `update_walk` every physics frame.
- Hand bones exist: `hand_r`, `hand_l`. Library clips exist: `Sword_Idle`, `Idle_Shield_Loop`, `Sword_Regular_A/B`, `Sword_Heavy_Combo`, `Punch_Jab`.
- `take_combat_events` keys today: `actor_id`, `target_id`, `part`, `damage`, `prevented`, `nullified`, `technique_id`, `attacker_item`, `defender_item`. No cooldown. `OpenCharacter` has no cooldown getter.
- `character_animation_test.gd` asserts `find_children(BoneAttachment3D)[0]` is Head. Hand sockets must be created at world equip time by CombatFeedback, not in the character scene `_ready`, so that suite still sees only hair when instantiating `male.tscn` / `female.tscn` alone.
- `combat_feedback_test.gd` asserts socket *names*, one mesh per item, spear both-slots one mesh, disabled limb keeps mesh, one motion per performed event, no motion for refused, labels on damage. It does not assert Visual tilt angle.

## File / interface ownership

| Files | Responsibility |
|---|---|
| `cpp/include/open/combat_rules.hpp`, `cpp/src/character_combat.cpp`, `cpp/tests/weapon_combat_test.cpp`, `cpp/tests/combat_session_test.cpp` | `SwingResult.attack_cooldown`; performed-swing oracle equals `weapon_cooldown` for that attempt; event carries it. Do not touch dirty `melee_swing_test.cpp`. |
| `extension/combat_bridge.hpp/.cpp`, `scripts/tests/combat_bridge_test.gd` | Dictionary key `attack_cooldown` (float, seconds). Absent/zero technique still 0. |
| `extension/item_bridge.hpp`, `extension/character_bridge.cpp`, `scripts/tests/weapon_bridge_test.gd` | Existing equipment snapshot gains `is_weapon`, `is_shield`, `held_usable` booleans, from C++ (no gameplay setter). |
| `scripts/character_model_pose.gd`, `scripts/tests/character_animation_test.gd` | Extra clip retarget; `set_armed_idle`; `play_attack(technique_id, duration, unarmed)`; walk/idle yield. |
| `scripts/combat_feedback.gd`, `scripts/tests/combat_feedback_test.gd` | Skeleton sockets, grip table, drive pose from events, delete Visual tween. |
| `docs/weapons.md` Current paragraph and decision-list presentation summary, only after visuals exist | Mark verified bone sockets and skeletal clips Current; preserve locked rules and explicitly retain any unresolved disabled-arm gate. |

No new GDExtension class. `player.gd` / `standing_dummy.gd` change only if `update_walk` gating cannot live entirely in the pose script.

## Verification protocol (execution only)

**SIM** (C++ tasks):

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build -R '^(weapon_combat|combat_session)$' --output-on-failure
```

**EXT/IMPORT** before any Godot test after C++/extension edits:

```bash
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
```

**GODOT** — current inventory (recount at execution; run each that exists):

```bash
godot --path . --headless --script res://scripts/tests/character_sheet_test.gd
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
godot --path . --headless --script res://scripts/tests/character_animation_test.gd
godot --path . --headless --script res://scripts/tests/character_popup_test.gd
godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd
godot --path . --headless --script res://scripts/tests/melee_combat_test.gd
godot --path . --headless --script res://scripts/tests/medieval_weapons_test.gd
godot --path . --headless --script res://scripts/tests/weapon_popup_test.gd
godot --path . --headless --script res://scripts/tests/weapon_bridge_test.gd
godot --path . --headless --script res://scripts/tests/technique_popup_test.gd
godot --path . --headless --script res://scripts/tests/equipment_popup_test.gd
godot --path . --headless --script res://scripts/tests/combat_feedback_test.gd
godot --path . --headless --script res://scripts/tests/workbench_test.gd
```

Desktop `godot --path .` is additional acceptance for grip and swing look. Do not claim it from headless.

## Tasks

### Task 1: Attack cooldown on the performed-swing report

**Dependencies:** none. **Files:** `combat_rules.hpp`, `character_combat.cpp`, `weapon_combat_test.cpp`, `combat_session_test.cpp`, `extension/combat_bridge.hpp/.cpp`, `scripts/tests/combat_bridge_test.gd` (required). **Cases:** `weapon_combat|combat_session`.

**Produces:** `SwingResult.attack_cooldown` (float seconds). Set once, from the same `weapon_cooldown(trained, speed, effectiveness, exhausted)` already assigned to the sheet. `take_combat_events` includes `attack_cooldown`. Refused attempts still produce no event. Gameplay unchanged.

- [ ] RED: a performed basic unarmed swing's `attack_cooldown` equals `attacker.attack_cooldown_remaining()` immediately after the attempt. Add paid and unpaid performed-swing fixtures with trained rank 100, unit speed and effectiveness: assert the independently expected 0.625 and 1.25 seconds respectively as well as the corresponding event values. Train through the existing C++ skill APIs in test setup, not debug setters. Separately test rank-1 exhaustion clamping to five seconds; do not infer exhausted scaling from a capped pair. A blocked/zero-damage performed swing still has a positive cooldown on the event. A cooling/out-of-range step still queues nothing. Bridge test: generate an event, advance the session partway without causing another swing, then drain it and assert its finite positive cooldown is the original duration, not the partially elapsed clock; the next drain is empty. Add this bridge test in Task 1, not conditionally.
- [ ] Observe SIM RED against the current struct: an expected missing-member compile failure is interface RED, not an executable assertion failure. Add the zero-initialized field first; rerun to observe an assertion RED, then assign `result.attack_cooldown = attack_cooldown_` after the existing clock assignment. Do not alter existing dirty combat logic or `weapon_cooldown` arithmetic.
- [ ] GREEN focused CTest, then extend the bridge dictionary, rebuild EXT/IMPORT and run `combat_bridge_test.gd`. Do not edit dirty tests.

### Task 2: Hand-bone sockets and per-item grip

**Dependencies:** none (sequential execution; do not edit concurrently in one worktree). **Files:** `scripts/combat_feedback.gd`, `scripts/tests/combat_feedback_test.gd`.

**Produces:** `_socket` finds the actor `Skeleton3D` under `Visual` and adds `BoneAttachment3D` `LeftHandSocket`/`RightHandSocket` with `bone_name` `hand_l`/`hand_r` if missing. Held meshes parent there. Two-hander still one mesh on right. Grip table applied as local transform. Instant unequip still frees the mesh. Disabled limb still keeps it. No Visual-wrapper offsets.

- [ ] RED: after equipping Sword right, the mesh's ancestor named `RightHandSocket` is a `BoneAttachment3D` whose `bone_name` is `hand_r`. Left equip → `hand_l`. Spear → one mesh, `hand_r`. Move the same item to the other hand without an intervening feedback tick; the mesh follows the new slot, without duplicates. Assert all five starter model keys have explicit finite grip transforms; do not require every transform to differ or claim this proves visual alignment. `Visual.rotation_degrees.x` remains 0 after attach. Hair `Head` attachment on a bare `male.tscn` instance is unchanged (animation suite).
- [ ] Implement sockets, instance/slot synchronization and grip table. Keep `Held_*` naming and item_id/model_key/slot metas. Check against the actual OBJ orientations and bone transforms; tune visually when a desktop is available. Do not play new clips here.
- [ ] GREEN `combat_feedback_test.gd` and `character_animation_test.gd` after import if any. Existing one-mesh / drain / damage-label cases must still pass.

### Task 2b: Expose item type and limb usability for idle selection

**Dependencies:** 2. **Files:** `extension/item_bridge.hpp`, `extension/character_bridge.cpp`, `scripts/tests/weapon_bridge_test.gd`. These are additive reads only; leave the simulation and dirty files alone.

- [ ] RED: in the existing real starter inventory, check carried Sword (`is_weapon=true`, `is_shield=false`, `held_usable=false`), equipped usable Sword (same type, `held_usable=true`), equipped Shield (`is_shield=true`, `is_weapon=false`), and an equipped two-handed Spear after disabling one arm (`held_usable=false` without losing the row or slots). Use existing bridge damage methods; no debug setters. Existing snapshot keys and tests stay intact. See the disabled-arm decision gate below before claiming one-handed side-specific behavior.
- [ ] Build the test fixture first, run `weapon_bridge_test.gd` to observe the missing-field failure, then extend `EquipmentSnapshotRow` and serialize the three bools as specified in technical default 6. Guard unconfigured characters, missing catalog/definitions, and non-held items; do not equate broken durability with unusable.
- [ ] Rebuild EXT/IMPORT, rerun `weapon_bridge_test.gd` green. Limb usability is evaluated on each snapshot, not cached by equipment signature.

### Task 3: Pose clips, armed idle, scaled attack playback

**Dependencies:** 1, 2, 2b. **Files:** `scripts/character_model_pose.gd`, `scripts/tests/character_animation_test.gd`, `scripts/combat_feedback.gd`, `scripts/tests/combat_feedback_test.gd`.

**Produces:** pose API:

```
func set_armed_idle(kind: StringName) -> void  # &"unarmed" | &"weapon" | &"shield"
func play_attack(technique_id: int, duration: float, unarmed: bool) -> void
```

Clip map as locked: `unarmed=true` always uses `Punch_Jab`; otherwise technique 0 or 1 → `Sword_Regular_A`, 2 → `Sword_Regular_B`, 3 → `Sword_Heavy_Combo`; unknown technique IDs use basic A as a presentation fallback. CombatFeedback passes `unarmed = int(event["attacker_item"]) == 0`, even if equipment has changed since the event was created. `play_attack` plays at `speed_scale = clip_length / duration` when duration is positive and finite; otherwise 1. Follow technical defaults 3–4 for looping, updates, restarting and finishing. CombatFeedback calls `set_armed_idle` every tick from C++-derived row data, and `play_attack` once per event; delete the Visual tween. The actor's model script is the child under `Visual`, not `Visual` itself; find it through `Visual` and `has_method`, as `player.gd` constructs it.

- [ ] RED on male and female fixtures: verify all six clips load; `Sword_Idle` loops; shield idle is selected; both exported custom idle/walk choices still work. With `play_attack(1, 2.0, false)`, verify A and `speed_scale == clip_length / 2.0`; repeated `update_walk(0.1, 1.0)` changes neither attack nor scale. Advance near the end and assert the same attack event restarts at time 0; advance through completion and verify the latest idle/walk and locomotion speed resume. Check A for 0/1, B for 2, Heavy for 3, Jab for `unarmed=true`, unknown ID fallback, and nonpositive/nonfinite duration fallback. Use the AnimationPlayer's manual advance plus a process frame if needed for completion signals.
- [ ] RED in `combat_feedback_test.gd`: a performed event increments `swing_motion_count` and leaves `Visual.rotation_degrees.x` at 0. A synthetic event with `attacker_item` nonzero, `technique_id` 3 and `attack_cooldown` 2.0 starts Heavy Strike on that actor's pose player; `attacker_item=0` starts Jab even if a weapon is currently equipped. A refused out-of-range order still adds no motion. Defender still none. Sword equip → weapon idle; equipped two-handed spear after one arm is disabled → fallback idle on next tick, without removing mesh; shield-only → shield idle; unequip → unarmed. If another usable weapon remains, weapon idle wins. All tests must check real player selection and no runtime errors, not just the counter.
- [ ] Before changing feedback to read the event's new field, add `attack_cooldown` to the three existing synthetic `small` / `nothing` / `big` dictionaries in `_tiny_and_technique_events_stay_legible` so Task 3's focused green run is reachable. Keep their 201/202/0 IDs and all existing assertions. Do not defer this fixture fix to Task 4.
- [ ] Implement retarget of the extra names, pose API, feedback wiring, remove `SWING_ANGLE` / tween. Missing clip: `push_warning`, still count the motion.
- [ ] GREEN animation + feedback tests, EXT/IMPORT if Task 1 landed in the same run.

### Task 4: Event integration, labels unchanged, regressions

**Dependencies:** 1, 2, 2b, 3. **Files:** `combat_feedback_test.gd` remaining cases, `combat_bridge_test.gd` if event shape is asserted, `docs/weapons.md` Current paragraph and presentation decision-list summary only.

**Produces:** live drained events (not only synthetic) drive clips with the C++ cooldown. Damage labels still appear on the event, still `.001` ≠ `"0"`, still sum to HP lost. No second damage application.

- [ ] Confirm `_tiny_and_technique_events_stay_legible` still produces one motion per synthetic event including `technique_id` 201/202/0, with the cooldown fields added in Task 3. Test a real drained event: capture its `attack_cooldown`, pass that same event to feedback, and assert the chosen clip's length / speed equals the captured value (not 0.28); do not drain twice or compare to a later partially ticked clock.
- [ ] Update `docs/weapons.md` **Current** and its presentation decision-list summary so neither still says meshes hang on `Visual` offsets or the swing is a tilt, once the tests above are green. Preserve all locked rules and any unresolved disabled-arm gate.
- [ ] Full CTest + EXT/IMPORT + all Godot scripts listed above. Desktop at 1280×720: inspect Sword, Dagger, Hammer_Small and Shield_Round in both applicable hands, and Spear on the right as a two-hander. For each, inspect grip alignment in idle and walk; for weapons also inspect a performed swing, ensure it is skeletal rather than a whole-body nod, and verify unequip removes the mesh. Confirm the shield stays in hand during a sword swing. Report each inspected case and list uninspected cases explicitly; headless transform assertions alone do not establish palm alignment.

## Coverage

| Requirement | Task | Evidence |
|---|---|---|
| Mesh on `hand_r`/`hand_l`, not Visual offset | 2 | BoneAttachment3D bone_name assertions |
| Spear one mesh on `hand_r` | 2 | existing spear case + bone_name |
| Per-starter grip, not combat | 2, 4 | Explicit transform entries plus per-model/per-hand desktop inspection; reach tests untouched |
| Instant equip/unequip | 2 | existing attach/remove cases |
| Armed idle / shield idle / Walk | 2b, 3 | weapon_bridge_test and character_animation_test |
| Clip only on performed event | 3–4 | swing_motion_count vs refused |
| Map A/B/Combo/Jab | 3 | pose fixture + synthetic technique_id |
| Duration = that swing's cooldown | 1, 3–4 | SwingResult oracle + speed_scale |
| Labels on event | 4 | existing label/HP sum |
| No tilt | 3–4 | Visual.rotation_degrees.x == 0 |
| Hair still Head | 2–3 | character_animation_test [0] == Head |
| Pre-existing work preserved | all | compare final file changes and status to execution-time baseline; only the scoped additive combat edit is permitted |

## Planning evidence and limits

- Re-review baseline: rebuilt the current GDExtension-enabled tree, then CTest passed 24/24; Godot import succeeded and all 13 existing `scripts/tests/*_test.gd` passed headlessly. These are baseline results, not evidence that this plan has been implemented.
- A headless import probe found `Idle_Shield` in UAL2 and `Sword_Idle` in UAL1. `Sword_Idle` imports non-looping; `Idle_Shield` loops. Attack clips import non-looping.
- The disabled one-handed-arm mismatch above is a genuine open gameplay decision. Grip numeric offsets remain tuning placeholders; desktop acceptance is still required.
- This revision edits only this plan file. No feature implementation.
