# Character Popup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan in the current session. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the player's live effective attributes and skills in an I-key popup, without showing resource pools.

**Architecture:** `open::Character` remains the source of truth. A read-only GDExtension method exposes named basic skill values and any chosen specialization; a Godot `Control` renders the player's existing attribute getters and that snapshot. The popup consumes clicks inside it, without pausing the world or changing simulation state.

**Tech Stack:** C++20, Godot 4.7, GDExtension, GDScript, headless Godot and C++ tests.

**Spec:** Approved conversation on 2026-10-28 and the character-screen mockup: two columns, attributes on the left, grouped scrollable skills on the right. I toggles the screen; × closes it. Show effective values, hide unchosen advanced skills, refresh while open, and omit HP, stamina and mana pools.

## Global Constraints

- Preserve all existing user changes. No gameplay mechanics or gameplay-state ownership changes.
- Keep Godot types out of `cpp/` simulation headers and sources.
- No read/write API for training or specializations solely for a test.
- After extension changes, rebuild `libopen.so` and re-import before Godot tests.
- Do not commit without a separate user request.

## Review Focus

- An unconfigured sheet or invalid ID must not throw or expose fake values.
- Level and blessings must affect displayed effective values, not just ranks.
- An unchosen advanced option must not appear; a chosen option must show its own value, including zero.
- Opening the popup must not intercept movement outside the popup, pause simulation, or leak popup clicks into the world.
- Repeated I key events, closing, scrolling, and changing data while open must behave consistently.

---

### Task 1: Expose effective skills without changing gameplay

**Files:**
- Create: `extension/character_skill_snapshot.hpp` for a Godot-independent read-only snapshot of a `const open::Character &`.
- Modify: `extension/character_bridge.hpp`, `extension/character_bridge.cpp` to expose `get_skill_snapshot() -> godot::Array`.
- Modify: `cpp/CMakeLists.txt` to register a focused snapshot test linked only to `open_sim`.
- Create: `cpp/tests/character_skill_snapshot_test.cpp`.
- Modify: `scripts/tests/character_sheet_test.gd` to test the actual bridge.

**Interfaces:**
- The snapshot helper returns 35 entries, in `BasicSkill` order. Each contains the rule name, `Character::effective_skill(basic)`, and an optional chosen advanced rule name plus `Character::effective_skill(basic, chosen)`.
- `OpenCharacter.get_skill_snapshot()` returns an array of dictionaries with `name`, `value`, `advanced_name`, and `advanced_value`; `advanced_name` is empty when unchosen. An unconfigured sheet returns an empty array. No skill IDs supplied by Godot are needed.

- [x] **Write failing C++ tests:** on a Strength-blessed level-1 character, assert 35 rows, One-Handed Weapons `1.125`, Dodging `1.0`, no unchosen advanced name; train One-Handed to 100 using `record_use`, select Katana and assert its name and zero effective value, then train it once to rank 1 and assert its effective value `1.125`. Verify the basic row remains its own effective value. Include level-7 and non-strength blessing cases.
- [x] **Run the new test** and confirm it fails because the snapshot helper does not exist.
- [x] **Implement the helper** using `skill_rule`, `Character::chosen`, `advanced_rule`, and `Character::effective_skill`, without Godot headers or duplicated skill rules.
- [x] **Run the C++ test** and `ctest --test-dir cpp/build --output-on-failure`; confirm all pass.
- [x] **Write failing bridge checks** for empty unconfigured snapshot and 35 configured rows with `1.125` and `1.0` values; verify the new method is absent before implementing it.
- [x] **Implement and bind `get_skill_snapshot()`**, converting helper records into Godot dictionaries without adding mutation APIs.
- [x] **Rebuild extension and import:** `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON`, `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build --output-on-failure`, and `godot --path . --headless --import --quit`.
- [x] **Run bridge test:** `godot --path . --headless --script res://scripts/tests/character_sheet_test.gd`; confirm its checks pass. Existing invalid-ID cases intentionally log errors.

### Task 2: Render and toggle the popup

**Files:**
- Create: `scripts/character_popup.gd` and, if needed, `scenes/character_popup.tscn` for the view.
- Modify: `scripts/main.gd` for the I-key toggle, player binding, and click isolation.
- Create: `scripts/tests/character_popup_test.gd`.
- Modify: `README.md` to document the shortcut and displayed values.

**Interfaces:**
- The popup binds to the player's existing `OpenCharacter`, reads `get_attribute(0..9)` and `get_skill_snapshot()`, and rebuilds/updates visible values while open.
- Rows use names and values from the bridge, with a chosen specialization shown beneath its basic skill. Show no other advanced names or resource pools.
- I toggles on a non-echo key press; × closes. Clicks inside the panel do not reach floor-click handling; the game remains unpaused.

- [x] **Write a failing headless popup test:** assert initially hidden; I opens, I closes, repeated key echo does not toggle, and × closes. Check all 10 attribute and 35 basic skill names; check effective `Strength 1.25` and `One-Handed Weapons 1.125`, no HP/stamina/mana pool rows, and no unavailable advanced rows. Feed a controlled chosen-specialization snapshot to the view and assert its name and zero value appear; change a displayed value while open and assert it refreshes. Verify an unconfigured sheet does not display invented data. Test a popup click does not issue a floor movement order, a click outside it still can, and an existing movement order continues while open. Verify a normal floor click still works when closed.
- [x] **Run the popup test** and confirm it fails because the popup does not exist.
- [x] **Implement the popup** using the approved two-column mockup, a scroll container for grouped skills, simple readable numbers, responsive margins, and standard Godot UI mouse filtering. Preserve the existing world and player code paths.
- [x] **Run Godot tests:** `godot --path . --headless --script res://scripts/tests/character_popup_test.gd`, `res://scripts/tests/character_sheet_test.gd`, `res://scripts/tests/character_visuals_test.gd`, and `res://scripts/tests/character_animation_test.gd`; confirm each passes.
- [ ] **Visually inspect** the popup at the project window size and a smaller viewport; verify legibility, scrolling, close behavior, and no shown resource pools. Record any interaction that could not be checked.
- [x] **Review the diff** and `git status` for accidental changes to user files or generated assets; update the README shortcut.
