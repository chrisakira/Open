# Validation findings -> fixes (2026-10-29)

Source: audit of docs/superpowers/plans/2026-10-29-melee-combat.md + docs/combat.md.
Baseline (fresh, before fixes): build exit 0, ctest 12/12, 6/6 Godot suites "all checks passed".

## Done

- [x] C1 refactor: kill_title() now reads title_rule(...).skill instead of a second copy of the mapping
- [x] C2 test: removed the self-comparing melee_cooldown assertion; added `exhausted_defence_pays_nothing`
      (mutation-proven: charging defence 8 stamina, and doubling its clock when tired, both go red)
- [x] C3 all-limbs-zero edge: PROVED UNREACHABLE instead of testing it. Limbs are 0.4 x main HP each
      (4 x 0.4 = 1.6x HP) and part hits charge main HP, so a sheet dies before its 3rd limb zeroes.
      Documented in docs/combat.md s6; no test written for an impossible state.
- [x] C4 doc: SwingResult.damage comment no longer claims "actually applied" past the clamp
- [x] E1 fix: remove_character holds a local Ref and reconciles survivors before it releases it
- [x] E2: try/catch barrier was the WRONG fix - this target is built -fno-exceptions, so a throw
      terminates rather than crosses. Removed the guards; eliminated the only throwing call here
      (std::random_device) and documented the contract in combat_bridge.hpp + .cpp
- [x] E3 fix: combat_bridge.cpp includes <godot_cpp/core/class_db.hpp>
- [x] G1 fix: AttackPopup.open_for_target refuses non-positive ids (written RED first: 3 failures)
- [x] G2 fix: _build_combat no longer hands combat to an actor the session refused
      + new `_every_actor_uses_the_coordinator` (mutation-proven by skipping one registration)
- [x] G3 test: `_marker_follows_attack_acceptance` covers both branches (mutation-proven by moving
      the marker hide outside the accepted branch)
- [x] G4 test: the "click outside the character sheet" skip is now a checked failure
- [x] G5 test: set_position assertion proves the position was stored (mutation-proven by no-op store)
- [x] G6 test: edge coordinates read root.get_visible_rect() (root.size is a 100x100 placeholder in
      headless - that mistake went red immediately and is fixed)
- [x] G7 doc: character_sheet_test comment matches current regen ownership
- [x] D1 doc: docs/combat.md "Current" section rewritten to what cpp/ actually does + s6 note
- [x] D2 doc: plan checkboxes - 34 marked done, 6 left open on purpose (5 red-phase observations with
      no retained log, 1 manual visual inspection never performed); Verification record appended
- [x] D3 fix: GODOT_CPP_DIR implemented in cpp/CMakeLists.txt (AGENTS.md promised it) - verified:
      honours a local checkout, FATAL_ERRORs on a bad path, and a clean -DOPEN_BUILD_GDEXTENSION=OFF
      configure+build+ctest is 12/12 with no godot-cpp
- [x] FINAL: full rebuild + ctest + import + all 6 Godot suites, fresh
      -> configure=0, build=0, ctest 12/12, import=0, all six suites exit=0 "all checks passed",
         zero compiler warnings from extension/, cpp/src, cpp/tests, cpp/include
- [x] FINAL: git status/diff review for generated or accidental changes
      -> no cpp/build, extension/bin, .godot or *.so tracked; untracked set is exactly the plan's
         deliverables + the 2026-10-28 popup work + .superpowers working notes; no MUTATION markers
         or scratch files remain

## Deliberately not changed

- `_try_move_to_click` shows the marker even when C++ refuses a destination. Pre-existing, and the
  plan pins that helper's name and `-> void` signature; changing it is a presentation decision for
  the human, not a gap in this plan.
- `build_roster() const` hands out non-const `Character*`. Forced by the plan's own
  `Combatant { Character *character; ... }`; documented rather than changed.
