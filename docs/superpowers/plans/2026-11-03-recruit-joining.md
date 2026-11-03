# Recruit joining implementation plan

> For later OpenCode jobs: implement only the single Task supplied by the
> manager, in sequence. Read Interfaces and Global Constraints first. Stop after
> that Task's verification. Do not start the next Task. Do not commit. Do not
> read `.superpowers/` or unrelated style essays. This document is a plan, not
> a roster, not item/title condition schemas, and not authorization to flip
> Jane's `can_move`.

**Goal:** Left-clicking living Jane opens the existing character popup with
Attack and Recruit. Choosing Recruit orders the currently commanded character
to walk into unarmed melee range 1.5, then Jane's dummy condition succeeds:
she joins the player's squad, stays Friendly, and her recruitable flag
clears. The enemy dummy stays Attack-only. No roster UI.

**Architecture:** `open::Character` owns recruitable, a tiny recruit-condition
enum, squad membership, the recruit-target id, and faction. `CombatSession`
issues the recruit order, walks it, runs the attempt once in range, and applies
the locked cancel/provoke exceptions. `OpenCombat` / `OpenCharacter` are thin
bridges. Godot draws the extra Recruit button and sends the target id.

**Tech stack:** C++20 `open_sim`, existing `test_support.hpp`, CMake/CTest,
Godot 4.7.2, GDExtension, GDScript headless tests.

**Spec:** `docs/recruiting.md` locked sections. `docs/combat.md` auto-retaliate
recruit-order exception. Do not implement Open item/title schemas or the roster.

## Live facts (inspect these, do not reinvent)

- Orders live on the sheet: `attack_target_` and `MoveOrder`. `set_destination`
  clears the attack; `set_attack_target` clears the move and interrupts work;
  `clear_order` clears both. Death in `Character::damage` already drops the
  attack.
- `CombatSession::issue_attack` / `issue_move` / `step` / `velocity_at` /
  `drop_dead_orders` are the coordinator. A performed swing auto-retaliates
  when the victim has **no attack order** via `set_attack_target`.
- Unarmed fallback range is `open::kMeleeRange = 1.5f` and `melee_in_range`
  (horizontal, Y ignored, inclusive). Attack pursuit currently uses **weapon
  reach**, not this constant. Recruit must use 1.5 even if a weapon is held.
- Attack popup: `scripts/attack_popup.gd`, one Attack button, `open_for_target`,
  `attack_requested`. `main._try_character_or_floor_click` opens it for any
  living `StandingDummy`. Floor click is `issue_move`.
- Scene: player id 1 `John Doe`; Jane id 2 at `(-6, 0.1, -2)` Friendly,
  `can_move` false; enemy id 3 at `(6, 0.1, -2)`. Jane is **not** recruited.
- Faction enum: `Friendly = 0`, `Enemy = 1`. No `set_faction` exists yet.
- Dirty tree at plan time (preserve, do not revert): status HUD/log scripts and
  docs, plus untracked `docs/recruiting.md`. No reset/stash/commit.

## Technical defaults (not new gameplay)

- Membership is `Character::in_squad()`, a bool on the **target** sheet. The
  player sheet is not auto-flagged. No Party object.
- `RecruitCondition`: `AlwaysSucceed`, `AlwaysFail`. Jane uses AlwaysSucceed.
  AlwaysFail exists so the locked fail path is testable without publishing a
  second playable recruit. No item/title predicates.
- Defaults at `Character::create`: `recruitable = false`,
  `condition = AlwaysFail`, `in_squad = false`.
- Scene sets Jane after `configure_npc`: recruitable true, AlwaysSucceed.
  Do not key anything on the string `Jane Doe 1`.
- `enum class RecruitCondition { AlwaysFail = 0, AlwaysSucceed = 1 };` lives in
  `character_rules.hpp` next to `Faction`. Do not put it in a new header.
- Recruit pursuit: `pursuit_velocity(self, target, kMeleeRange, walk_speed())`
  from `weapon_rules.hpp`. Do **not** call `attack_velocity_at`. Long-weapon
  fixture uses the existing starter spear (`kStarterSpear`, max reach 2.0) via
  the same add/equip path `weapon_combat_test.cpp` already uses. Do not invent
  an item.
- At most one recruit attempt per actor per **positive** `step`. Zero-time
  `step` reconciles (dead/missing target) and does not run the check.
- **Frame order in `step_core`:** clocks → swings (provoke sees a live recruit
  order) → drop dead orders → then recruit attempts for actors that still hold
  a recruit target and are in `melee_in_range`. A hit in the same frame cancels
  recruit and the attempt does not run.
- Stun: same as walk/attack — velocity zero, order kept.
- `set_recruit_target` interrupts work with `WorkFailure::Movement` (same as
  `set_destination`). Do not add a WorkFailure.
- Success/fail spend nothing.
- GDScript may call `set_recruitable` / `set_recruit_condition` (scene setup,
  like `set_can_move`). GDScript must not set faction or `in_squad` directly.
- Jane stays `set_can_move(false)` after joining.

## Global constraints

- Open at `~/Projects/Open`, not Project_OPEN. Preserve dirty HUD work.
- C++ owns flags, orders, range, the attempt, membership, faction-on-success,
  death-leave, and the provoke exception. Godot presents and sends ids.
- No Godot types in `cpp/`.
- No roster, no control switching, no camera change, no lore, no gold, no
  Diplomacy formula, no item/title condition language.
- Do not treat Jane's dummy success as "every recruit succeeds".
- Standing dummies keep `can_move false`. Spike ids 1/2/3 and factions 0/1 stay.
- Every Task: failing tests first, observe red, implement only that Task,
  then the Task's tests and full `ctest`. Godot Tasks rebuild `libopen.so`,
  `ctest`, `godot --path . --headless --import --quit` before scripts.
- C++-only Tasks: `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF`.
- Before any Godot script after C++/extension changes:
  `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON`,
  `cmake --build cpp/build -j"$(nproc)"`,
  `ctest --test-dir cpp/build --output-on-failure`,
  `godot --path . --headless --import --quit`.
- Use existing create/damage/order APIs in fixtures. No test-only HP setters.
- No commit. No `cpp/build/`, `extension/bin/`, `.godot/`, `*.so`.

## Out of scope

| Thing | Why |
|---|---|
| Roster / click-name-to-control | Next slice |
| Item, title, skill-check schemas | Open |
| Dismiss / desert | Locked: death only |
| Enemy starting recruitable | Scene lock: Jane only |
| Flipping Jane `can_move` | Not asked |
| Cost | Locked none for now |

## Coverage matrix

| Behavior | Live today | This plan |
|---|---|---|
| Click living dummy | Attack popup, Attack only | Same popup; Recruit iff `recruitable` |
| Jane | Friendly, not recruitable, cannot walk | Recruitable, AlwaysSucceed, still cannot walk |
| Enemy | Attack only | Unchanged |
| Floor click during recruit | n/a | Cancels recruit, walks there, not a fail |
| Attack during recruit | n/a | Cancels recruit, issues attack, not a fail |
| Swing at recruiter | Auto-retaliate if no attack order | Cancel recruit, **no** auto-retaliate, stop |
| Target dies on the way | n/a | Order ends, flag/faction unchanged |
| Jane success | n/a | `in_squad`, Friendly, recruitable false |
| Fail fixture | n/a | Not in squad, faction unchanged, recruitable false |
| Member dies | n/a | `in_squad` false; no dismiss |

## Review focus

- Weapon reach must not replace 1.5 for recruit (Task 2).
- Auto-retaliate must not fire on a recruit-order victim (Task 2).
- Cancels must not count as fail (flag stays) (Task 2).
- Success/fail both clear recruitable; only success sets squad+Friendly (Tasks 1–2).
- Popup Recruit clicks must not be floor moves; Attack still works on Jane and the enemy (Task 4).
- Dirty HUD files must remain except intentional popup/main wiring (Task 4).

---

### Task 1: Sheet flag, condition, membership, faction, death-leave

**Files:**
- Modify: `cpp/include/open/character.hpp`, `cpp/src/character.cpp`,
  `cpp/include/open/character_rules.hpp` (enum only if it belongs with Faction;
  otherwise keep the enum next to Character).
- Create: `cpp/tests/recruit_sheet_test.cpp`.
- Modify: `cpp/CMakeLists.txt` — executable `recruit_sheet_tests`, CTest name
  `recruit_sheet`.

**Interfaces:**
- `enum class RecruitCondition { AlwaysFail, AlwaysSucceed };`
- On `Character`: `recruitable() const`, `set_recruitable(bool) -> bool`,
  `recruit_condition() const`, `set_recruit_condition(RecruitCondition) -> bool`,
  `in_squad() const`, `recruit_condition_passes() const`.
- `set_recruitable` / `set_recruit_condition` refuse when dead (no mutation).
  Living sheets accept. Create defaults: not recruitable, AlwaysFail, not in
  squad.
- `recruit_condition_passes`: AlwaysSucceed true, AlwaysFail false. Does **not**
  join, clear flags, or spend. Dead sheet: false.
- No public `set_faction` / `set_in_squad`. Task 1 does **not** apply success
  or death-leave. It only stores flag, condition, and `in_squad` default false.

- [ ] **RED:** `create_defaults_are_not_recruitable` — fresh sheet: recruitable
  false, AlwaysFail, `in_squad` false, faction Friendly or Enemy as configured,
  `recruit_condition_passes` false.
- [ ] **RED:** `living_sheet_sets_flag_and_condition` — set recruitable true and
  AlwaysSucceed; `recruit_condition_passes` true. AlwaysFail after that is
  false. Dead sheet (torso kill via existing `damage`) refuses both setters;
  values unchanged from before death.
- [ ] **RED:** `condition_is_not_join` — AlwaysSucceed + recruitable does not
  change faction or `in_squad`.
- [ ] Observe red: configure OFF, build `recruit_sheet_tests`, failure is
  missing APIs not a missing target.
- [ ] Implement only the storage and reads/setters/eval.
- [ ] GREEN: `ctest --test-dir cpp/build -R '^recruit_sheet$' --output-on-failure`
  then full `ctest --test-dir cpp/build --output-on-failure`. Stop.

### Task 2: Recruit order, walk, attempt, cancels, provoke exception

**Files:**
- Modify: `cpp/include/open/character.hpp`, `cpp/src/character.cpp` —
  `set_recruit_target`, `recruit_target()`, `recruit_velocity_at`, clear hooks
  on `set_destination` / `set_attack_target` / `clear_order` / death.
- Modify: `cpp/include/open/combat_session.hpp`, `cpp/src/combat_session.cpp` —
  `issue_recruit`, step attempt, drop dead recruit orders, velocity, provoke.
- Create: `cpp/tests/recruit_order_test.cpp`.
- Modify: `cpp/CMakeLists.txt` — `recruit_order_tests` / CTest `recruit_order`.
- Modify existing `cpp/tests/combat_session_test.cpp` only if an existing
  provoke fixture would break; prefer new tests in `recruit_order_test.cpp`.

**Interfaces:**
- `Character::set_recruit_target(std::uint64_t target_id) -> bool` — refuse
  zero, self, dead actor; on accept store id, clear move and attack, interrupt
  work. Does **not** look up the target sheet (session does).
- `recruit_target() const -> std::optional<std::uint64_t>`
- `recruit_velocity_at(self, target) const -> Vec3` — zero if no recruit order,
  cannot walk, stunned, or `melee_in_range`; else unarmed 1.5 near-side walk.
  Held weapon reach must not change this (fixture: give a long weapon, still
  1.5).
- `set_destination` accepted: clears recruit target (not a fail).
- `set_attack_target` accepted: clears recruit target (not a fail).
- `clear_order` / death of the **recruiter**: clear recruit target.
- `CombatSession` is already `friend class CombatSession` on Character. Add
  private `apply_recruit_success()` / `apply_recruit_failure()` on the **target**
  sheet (success: in_squad true, faction Friendly, recruitable false; fail:
  recruitable false, in_squad unchanged false, faction unchanged). Session
  clears the recruiter's recruit order after either. Do not add public
  `set_in_squad` or `set_faction`.
- `CombatSession::issue_recruit(actors, actor_id, target_id) -> bool` — refuse
  bad roster, missing/self/dead, target **not recruitable**, actor dead.
  Rejected calls leave the old order. Accepted: `set_recruit_target`. Faction
  is not a filter.
- `velocity_at`: if recruit target live, `recruit_velocity_at`; else existing
  attack/move behavior.
- `drop_dead_orders` (or sibling): if recruit target dead or absent, clear
  recruit order **without** fail (flag/faction unchanged).
- Positive `step` follows the Global frame order. After swings and drop-dead,
  if the actor still holds a recruit target and is in `melee_in_range` and the
  target is still recruitable, run **one** attempt via the private apply
  helpers. Out of range: keep walking, no attempt.
- Provoke: a performed swing at a victim that **holds a recruit order** does
  **not** `set_attack_target`. Clear the victim's recruit order only. Victim
  has no attack order. Not a fail for **that victim's** recruit (they were the
  recruiter). Flag/faction of the **recruit target** unchanged.
- Existing provoke when the victim has no recruit order and no attack order
  stays.
- Death of a character with `in_squad`: `in_squad` becomes false. Do this in
  `Character::damage` next to the existing attack-clear.

- [ ] **RED:** `issue_recruit_requires_flag` — recruitable Jane-shaped target
  accepted; non-recruitable enemy-shaped target refused; old attack/move kept
  on refuse; self/zero/dead refused.
- [ ] **RED:** `walk_uses_unarmed_1_5` — actor at `(0,0,0)`, target `(10,7,0)`,
  recruitable AlwaysSucceed: velocity x positive, y 0; in range at 1.5 and
  1.4999 (zero vel); out at 1.5001 (nonzero). A long-reach weapon on the actor
  does not change that. `can_move false` yields zero velocity and keeps the
  order.
- [ ] **RED:** `in_range_always_succeed_joins` — start already in range,
  AlwaysSucceed, Friendly target: one positive step joins, faction Friendly,
  recruitable false, recruiter order gone, stamina/mana unchanged.
- [ ] **RED:** `always_fail_is_one_shot` — AlwaysFail, Enemy target that was
  recruitable: after attempt, not in squad, still Enemy, recruitable false,
  recruiter order gone.
- [ ] **RED:** `move_and_attack_cancel_without_fail` — out of range order, then
  `issue_move` / `issue_attack`: recruit gone, target still recruitable, not
  in squad.
- [ ] **RED:** `target_death_cancels_without_fail` — kill target via `damage`
  while walking; next step/reconcile: recruiter order gone, if you snapshot
  flag before death it was still recruitable (death itself is not fail).
- [ ] **RED:** `hit_while_recruiting_does_not_retaliate` — recruiter walking a
  recruit order; another actor performs a swing at the recruiter; recruiter
  has no attack order, no recruit order, recruit **target** still recruitable
  and not in squad.
- [ ] **RED:** `death_removes_from_squad` — After success, kill the member;
  `in_squad` false.
- [ ] Observe red, implement, GREEN `recruit_order` then full ctest. Also run
  `combat_session` so provoke fixtures still pass. Stop.

### Task 3: Bridge

**Files:**
- Modify: `extension/character_bridge.hpp`, `extension/character_bridge.cpp`,
  `extension/combat_bridge.hpp`, `extension/combat_bridge.cpp`.
- Create: `scripts/tests/recruit_bridge_test.gd` (same guards as
  `combat_bridge_test.gd`).
- Modify: `scripts/tests/combat_bridge_test.gd` only if signatures collide;
  prefer not.

**Interfaces:**
- `OpenCharacter`: `is_recruitable() -> bool`, `set_recruitable(bool) -> bool`,
  `get_recruit_condition() -> int` (0 AlwaysFail, 1 AlwaysSucceed),
  `set_recruit_condition(int) -> bool`, `is_in_squad() -> bool`,
  `get_recruit_target_id() -> int` (0 if none). Unconfigured: false / 0.
  Invalid condition int refused. No `set_in_squad`, no `set_faction`.
- `OpenCombat.issue_recruit(actor_id, target_id) -> bool` — same rejection
  rules as C++, no exceptions into Godot.

- [ ] **RED:** missing methods fail as counted assertions, not script abort.
  Jane-like sheet: set recruitable + AlwaysSucceed, `issue_recruit(1, 2)` at
  in-range positions, `step`, `is_in_squad` true, faction 0, recruitable false.
  Enemy without flag: `issue_recruit` false.
- [ ] Rebuild ON, ctest, import, run the new script. GREEN. Also
  `combat_bridge_test.gd`. Stop.

### Task 4: Popup Recruit button and scene Jane

**Files:**
- Modify: `scripts/attack_popup.gd` — Attack stays; Recruit button named
  `RecruitButton`; show Recruit only when opened as recruitable.
- Modify: `scripts/main.gd` — pass recruitable from the dummy's sheet into
  `open_for_target`; Jane setup sets recruitable + AlwaysSucceed; wire
  `recruit_requested` to `issue_recruit`.
- Modify: `scripts/tests/melee_combat_test.gd` — Attack-only on the enemy
  still; Jane's popup can show Recruit without breaking Attack.
- Create: `scripts/tests/recruit_joining_test.gd` — real main scene.

**Interfaces:**
- `AttackPopup.open_for_target(target_id, screen_position, recruitable := false)`
  — third arg default false so old call sites stay Attack-only.
- Signal `recruit_requested(target_id: int)` — emit on Recruit, close, do not
  issue the order.
- Opening/dismissing still issues nothing. Escape/outside click unchanged.
- `main` after Jane `setup(...)`: `sheet.set_recruitable(true)` and
  `sheet.set_recruit_condition(1)`. Enemy: do not set. Player: do not set.
- Style: same rustic button as Attack (`docs/style.md` palette already on the
  popup). Keep the panel small; two buttons stacked or side by side, still
  STOP mouse filter.

- [ ] **RED:** `recruit_joining_test.gd` instantiates the main scene, waits
  for ready. Jane `is_recruitable() == true`, enemy false, player false.
  Open popup on Jane: RecruitButton visible. Open on enemy: Recruit hidden,
  Attack visible. Choosing Recruit with Jane already placed in range (move
  the player node, publish position, `issue_recruit` or click Recruit) then
  enough `step`/physics that C++ is in range: Jane `is_in_squad()`, faction
  0, recruitable false. Floor click while a pending out-of-range recruit
  exists (if the test issues out of range first) cancels without joining.
  Existing melee Attack on the enemy still opens and can issue attack.
- [ ] Observe red (missing Recruit / Jane not flagged).
- [ ] Implement popup + Jane flags + main wiring only.
- [ ] GREEN: import if signatures changed, then
  `godot --path . --headless --script scripts/tests/recruit_joining_test.gd`,
  `scripts/tests/melee_combat_test.gd`, and the status tests that mention
  AttackPopup (`status_integration_test.gd`) so HUD dirty work still passes.

## Verification (after all Tasks)

Manager, not the coder, re-runs:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
godot --path . --headless --script scripts/tests/recruit_joining_test.gd
godot --path . --headless --script scripts/tests/melee_combat_test.gd
godot --path . --headless --script scripts/tests/combat_bridge_test.gd
godot --path . --headless --script scripts/tests/recruit_bridge_test.gd
```

Desktop visual: click Jane, see Attack and Recruit; Recruit walks in and she
joins (diamond stays green; Recruit disappears next open). Click enemy: Attack
only. Not done in headless.

## What this does not ship

Roster, switching who you command, Jane walking on her own, item/title
conditions, a way to retry Jane after a fail (scene Jane cannot fail).
