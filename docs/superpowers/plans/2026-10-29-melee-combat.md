# Melee Combat Implementation Plan

> **For later OpenCode jobs:** Implement only the single Task supplied by the human, in sequence. Read its Interfaces and the Global Constraints first. Steps use checkbox (`- [ ]`) syntax for tracking. Stop after that Task's verification; do not start the next Task or commit without a separate human request. This document is a plan, not authorization to begin Task 1.

> **Superseding rule (2026-10-29, human-authorized corrections):** A newly chosen advanced skill starts at rank **1** with zero residual XP, and a chosen track runs **1..200**. Where this plan says "rank-0 Boxing", "rank-0 advanced dodge" or asserts a fresh specialisation is rank 0, read "rank 1"; the `zero_damage_trains_nothing` fixture is replaced by a positive one (an unlocked specialisation immediately lands a real hit). Also, `toughness_base` in the full-hit formula is the **defender's** base Toughness. `advanced_xp_to_next(0) == 8` stays as arithmetic; the `trained = 0` rows in the cooldown/damage tables stay as formula floors, and unchosen advanced options remain hidden. This note does not change the task checklists below.

**Goal:** Left-clicking a living Friendly or Enemy dummy opens a tiny Attack popup; choosing Attack orders the player to approach and auto-swing, applying C++ damage to a random body part until cancelled or the fight ends.

**Architecture:** `open::Character` owns its attack order and attack/defend clocks. Small Godot-independent combat rules select trained skills and resolve swings; a `CombatSession` resolves target IDs against a supplied roster of existing characters and positions, computes combat state, and advances combat and regeneration once. A thin `OpenCombat` extension adapter retains the existing `OpenCharacter` objects; Godot supplies positions, displays the popup, sends IDs, and applies C++ movement intent through its existing physics/visual code. This is not a new world framework or Project_OPEN architecture.

**Tech Stack:** C++20, existing `open_sim` and `test_support.hpp`, CMake/CTest, Godot 4.7.2, GDExtension with godot-cpp `godot-4.5-stable`, GDScript and headless Godot tests.

**Spec:** `docs/combat.md` sections 1–9 are locked; copy their formulas, not older sheet prose about effective combat attributes. `docs/characters.md` supplies existing limbs, equipment gates, skill progression, titles, spending and regeneration. Read `AGENTS.md` first. Section 10 and sentences explicitly marked Open are not build orders.

## Global Constraints

- This is Open at `~/Projects/Open`, not Project_OPEN. Preserve existing uncommitted changes, including the character-popup work. Do not replace the character sheet or copy another project's architecture.
- C++ owns attack orders, target validity, horizontal range, attack/block/dodge clocks, hit resolution, damage, `in_combat`, skill XP and title progress. Godot only presents, supplies positions/time and sends intent. No Godot headers or godot-cpp dependency in `cpp/` / `open_sim`.
- Placeholders: melee range `1.5`, weapon `8`, armour `0`, shield `0`. A usable weapon used to block has block value `8`; an unarmed dodge has shield `0`. No item catalog or variable item stats. The scene starts unarmed and uses Unarmed Combat.
- `attack_skill = weapon trained / 200`; `defend_skill = Blocking or Dodging trained / 200`. Combat uses trained ranks, not `effective_skill`, effective Strength, effective Agility, level multipliers or the sheet's ×1.1 effective-skill title modifiers.
- Full hit: `max(0, weapon * attack_skill - toughness_base * armour)`. Toughness is **base**. Below 8 stamina, halve `weapon * attack_skill` before subtracting soak, not the final damage.
- Nullify **only** when the defend clock is ready **and** `shield + defend_skill > attack_skill * weapon`. Equality does not nullify. Keep that comparison as written; do not add a miss roll, chance roll or faction exception.
- Attack cooldown: `t = trained / 200`, `rate = 0.2 + 2.8 * t`, `cooldown = 1 / rate`; clamp to `[1/3, 5]`, double for an unpaid/low-stamina swing, then clamp again. Defense uses the same rank/rate formula and bounds, without inventing a defense stamina cost or exhaustion penalty.
- At stamina ≥ 8, pay the existing `spend(ResourceUse::MeleeSwing)` cost of 8. Below 8, still swing, spend nothing extra, halve the offensive damage term and double/clamp that swing's attack cooldown. This is per swing, not a persistent debuff.
- Attacker: exactly 1 skill XP only when damage > 0. Defender: exactly 1 matching defense skill XP only on nullify. Neither changes main level. Preserve Duelist/Breaker kill progress and Wall successful-block progress using existing title APIs; a dodge is not a Wall block. Dragons do not exist; do not invent dragon targets or apply Dragonslayer to these dummies.
- Pick uniformly among Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg, using C++ randomness. Apply positive damage exactly once through `Character::damage(part, amount)`; do not subtract main HP separately.
- Preserve `MoveOrder::kArriveRadius = 0.4`, C++ walk speed `4.5 * leg multiplier`, horizontal movement ignoring height, green Friendly/red Enemy presentation, IDs 1/2/3 and faction bridge values 0/1. Standing dummies retain `set_can_move(false)` and receive no automatic attack order. Passive block/dodge is not retaliation.
- A successful move order cancels an attack; a rejected move request must not destroy it. Clear, death, target death/removal and inability to close because both legs are zero stop the appropriate attack. Do not select another target automatically or reset cooldowns by issuing/cancelling orders.
- Unarmed fallback needs at least one surviving arm or leg. Do not invent Boxing-versus-Taekwondo limb-specific rules, crawling, or new all-limbs-disabled behavior beyond existing death/capability gates. Both legs at zero cancel an out-of-range pursuit; do not prohibit an otherwise usable in-range attack merely because `can_move` is false.
- No bows, crossbows, spells, squads, multiple-attacker systems, auto-retaliation, crits, aimed attacks, extra friendly-fire rules, new cameras, combat animations or extra combat UI. Existing idle/walk visuals and labels remain.
- Every Task starts with focused failing tests, observes the failure, implements only that Task, then runs CTest. Tasks touching Godot also run headless Godot checks. Use existing training APIs to prepare C++ fixtures; no test-only setters for ranks, XP, HP or cooldowns.
- Before **any Godot run after C++/extension changes**, rebuild `libopen.so` and import: `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON`, `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build --output-on-failure`, `godot --path . --headless --import --quit`, in that order.
- Do not commit unless the human later asks. Do not stage generated `cpp/build/`, `extension/bin/`, `.godot/` or `*.so`. No Task below includes a commit step.

## Review Focus

- Range boundaries and approach geometry: stop inside horizontal 1.5 without walking into the target or stalling outside range because walking's arrival radius is 0.4 (Tasks 1, 6).
- Stale intent: invalid IDs, rejected moves, retargeting, target death/removal and self-death must not retain a harmful swing, accidentally replace a valid order or reset attack/defend clocks (Tasks 1, 3–6).
- Combat arithmetic and track selection: equality must not nullify; fatigue halves only the offensive term; advanced rank 0 and frozen-parent fallback must not become effective-attribute damage or free XP (Tasks 2, 3).
- Time/state ownership: the passive defender is in combat without retaliating; each character's clocks and regeneration advance once, including after cancellation, and invalid time never mutates simulation (Tasks 3–6).
- Input isolation: character picking must win over the floor behind it; Attack clicks must not move the player; the existing I-key popup must still isolate its own clicks while floor clicks outside panels continue to work (Task 6).

---

### Task 1: Add the simulation attack order and melee approach geometry

**Files:**
- Create: `cpp/include/open/combat_rules.hpp`, `cpp/src/combat_rules.cpp` for the melee range constant and range predicate; later Tasks extend these files.
- Modify: `cpp/include/open/character.hpp`, `cpp/src/character.cpp` for attack intent and its movement query.
- Create: `cpp/tests/attack_order_test.cpp`.
- Modify: `cpp/CMakeLists.txt` to add the combat-rules source to `open_sim` and register `attack_order_tests` / CTest name `attack_order`.

**Interfaces:**
- Produces in namespace `open`: `inline constexpr float kMeleeRange = 1.5f;` and `bool melee_in_range(const Vec3 &self, const Vec3 &target);`, measuring X/Z only, inclusive at 1.5; invalid horizontal inputs return false.
- Produces `Character::set_attack_target(std::uint64_t target_id) -> bool`, `attack_target() const -> std::optional<std::uint64_t>` and `attack_velocity_at(const Vec3 &self, const Vec3 &target) const -> Vec3`.
- Store only the optional target ID, not a target pointer or position. `set_attack_target` rejects zero, self and a dead actor without changing the existing order; living-target lookup belongs to Task 4. An accepted attack clears the move order. An accepted `set_destination` clears the attack; `clear_order` clears both. Death clears the actor's attack.
- `attack_velocity_at` returns zero without an attack, when in range or when walking is disabled. Out of range it uses the existing `MoveOrder` math and `walk_speed()` toward the point on the near side of the target offset by `1.5 - 0.4 = 1.1`, at the actor's height. This puts the walking arrival band at melee range rather than 0.4 beyond it. Invalid inputs return zero. Ordinary `velocity_at(px, py, pz)` remains the move-order query and returns zero for an attack-only order.
- This Task deliberately has no roster, swings, clocks, stamina spending, damage, XP, bridge or Godot changes.

- [x] **Write failing order tests:** `attack_replaces_move`, `move_and_clear_cancel_attack`, `invalid_intent_is_atomic`, `death_cancels_attack`. Assert target IDs round-trip; a move at `(8,0,0)` is replaced by target 2; a valid new move removes target 2; zero/self attack IDs and a NaN destination preserve the preceding valid order. Kill via the existing torso-damage API and assert no attack and no new attack accepted. Retarget from 2 to 3 without introducing faction filtering.
- [x] **Write failing range/movement tests:** `horizontal_melee_boundary` asserts distances 1.4999 and 1.5 are in range, 1.5001 is out, and a large Y difference changes neither result. `approach_stops_beside_target` asserts self `(0,0,0)`, target `(10,7,0)` yields velocity `(4.5,0,0)`, including nonzero velocity at separation 1.5001, and zero at 1.5 and coincident X/Z. Assert ordinary point movement still stops at 0.4, one disabled leg yields 2.25, and `set_can_move(false)` yields zero. Include non-finite X/Z and an overflowing separation without NaN velocity.
- [ ] **Configure and build the red test:** run `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF`, then `cmake --build cpp/build --target attack_order_tests -j"$(nproc)"`. Confirm the missing combat interfaces cause the intended failure, not a missing test registration or unrelated error.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*
- [x] **Implement only the interfaces above**, preserving the existing `MoveOrder` implementation and spike constants. Validate a move before discarding an attack, and use finite-safe horizontal distance calculations. Do not resolve or cache target objects yet.
- [x] **Verify green:** run `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build -R '^attack_order$' --output-on-failure`, then `ctest --test-dir cpp/build --output-on-failure`. Expect all tests to pass. Stop here; do not start swing resolution or Task 2.

### Task 2: Define exact combat arithmetic and resolve trained loadouts

**Files:**
- Modify: `cpp/include/open/combat_rules.hpp`, `cpp/src/combat_rules.cpp` for pure math and profile selection.
- Modify: `cpp/include/open/equipment.hpp`, `cpp/src/equipment.cpp` for minimal optional combat tags and a read-only item lookup.
- Modify: `cpp/include/open/character.hpp`, `cpp/src/character.cpp` to forward that lookup.
- Create: `cpp/tests/combat_rules_test.cpp`.
- Modify: `cpp/tests/equipment_test.cpp`, `cpp/CMakeLists.txt`; register `combat_rules_tests` / CTest name `combat_rules`.

**Interfaces:**
- Consumes Task 1's `kMeleeRange` and existing `Character::{basic,advanced,chosen,equipped,held_usable,body_part}`.
- Extend `ItemRef` with `std::optional<BasicSkill> combat_skill` and `std::optional<AdvancedSkill> combat_option`, both empty by default. These identify a held weapon's job or a shield, not item stats. Existing untagged records remain valid: usable OneHanded/TwoHanded records use their generic weapon basic; a `HandUse::None` record is not a weapon.
- Tagged weapon basics may be OneHandedWeapons, TwoHandedWeapons or Polearms; Blocking identifies a shield. An optional advanced tag must belong to that basic. OneHandedWeapons/Blocking require OneHanded hand use; TwoHandedWeapons/Polearms require TwoHanded. Refuse malformed tags atomically in `Equipment::add`, including an advanced tag without its basic. Do not add catalog entries, per-item damage, armour, reach or equipment UI.
- Produce `Equipment::item(ItemId id) const -> std::optional<ItemRef>` and the same `Character::item` signature, returning a copy from storage or equipped slots, or empty for unknown/zero IDs. Preserve two-handed storage/reservation behavior.
- Produce `AttackProfile { BasicSkill basic; std::optional<AdvancedSkill> option; unsigned trained; }`, `enum class DefendAction { Block, Dodge };` and `DefendProfile { DefendAction action; BasicSkill basic; std::optional<AdvancedSkill> option; unsigned trained; float shield; }`.
- Produce `attack_profile(const Character &) -> std::optional<AttackProfile>` and `defend_profile(const Character &) -> std::optional<DefendProfile>`. Dead characters return empty. Attack: select one usable non-shield weapon; otherwise UnarmedCombat with chosen Boxing/Taekwondo, requiring a surviving arm or leg. For this single-weapon slice, inspect LeftHand then RightHand and deduplicate a two-handed ID; do not invent dual-wield attacks.
- Defense: usable shield first, otherwise usable two-handed weapon, then one-handed weapon, otherwise Dodging with chosen Footwork/AcrobatEvasion. Shield tags select matching shield specialization; weapon blocking uses basic Blocking, including frozen 100 when a shield specialization is chosen. Shield/weapon block value is 8; dodge value is 0. Apply `held_usable` gates at query time, not only when equipping.
- For a tagged weapon/shield job, carry its `combat_option` into the profile even if it is unchosen. Read that advanced rank only if it is the chosen option; otherwise read the basic rank. This mirrors existing `record_use(basic, option, true)` fallback semantics. A generic weapon without a subtype uses the basic, not an arbitrary chosen weapon specialization.
- Produce `melee_cooldown(unsigned trained, bool exhausted) -> float`, `melee_damage(unsigned trained, float weapon, float toughness_base, float armour, bool exhausted) -> float` and `melee_nullifies(bool defend_ready, float shield, unsigned defend_trained, unsigned attack_trained, float weapon) -> bool`. Use the Global Constraints' formulas exactly, with floating-point division by 200. Runtime callers supply valid ranks and nonnegative finite placeholder values.

- [x] **Write failing arithmetic tests:** `cooldown_uses_trained_rank` checks ranks `0,1,100,200` against `5, 1/0.214, 0.625, 1/3`; exhausted results are `5,5,1.25,2/3`. `damage_uses_base_soak_and_offensive_half` checks trained 1/weapon 8/armour 0 → 0.04, exhausted → 0.02; trained 100/weapon 8/base Toughness 2/armour 0.5 → 3, exhausted → 1, and excessive soak → 0. `nullify_is_ready_and_strictly_greater` checks shield 0/defend trained 200/attack trained 25/weapon 8: equality gives false; attack trained 24 gives true only when ready; a high shield with a non-ready clock still gives false.
- [x] **Write failing profile tests:** `unarmed_uses_chosen_track`, `matching_weapon_or_frozen_parent`, `defense_priority_and_limb_gates`, `effective_attributes_do_not_enter_combat`. Train with existing `record_use` until the needed rank, then advance; assert fresh Boxing rank 0, and Katana chosen while an Axe-tagged weapon uses frozen OneHandedWeapons 100. Generic one-/two-handed and tagged polearms use their correct basics. Matching shield specialization is used only for that shield; mismatched shields and weapon blocks use basic Blocking. Breaking one arm disables a two-handed weapon and falls back to unarmed when no other weapon is usable; breaking both arms leaves dodge/unarmed available through healthy legs. Level 7, high Strength/Agility, Toughness blessing and Duelist must not change the trained-rank attack value or cooldown. Nonzero-armour helper tests pass `Character::base(Toughness)`, not its effective value.
- [x] **Write failing equipment tests:** default untagged records still equip; valid weapon/shield tags round-trip through storage and both views of a two-handed reservation. Unknown item lookup is empty. Invalid skill IDs, non-melee basics, wrong-parent options and incompatible hand-use tags fail without losing the item or changing slots. Confirm dead/disabled held items are not selected as usable weapons or blocks.
- [ ] **Observe red:** run `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF` and `cmake --build cpp/build --target combat_rules_tests equipment_tests -j"$(nproc)"`; confirm failures name the missing interfaces.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*
- [x] **Implement the lookup/tags, profiles and pure formulas**, reusing existing enum/rule tables and character reads. Keep equipment identity/ownership logic in `Equipment`; keep combat selection and arithmetic in `combat_rules`. Do not implement swings or advance time yet.
- [x] **Verify green:** run `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build -R '^(combat_rules|equipment)$' --output-on-failure`, then full `ctest --test-dir cpp/build --output-on-failure`. Stop after this Task.

### Task 3: Resolve one swing with independent attack/defend clocks, damage and XP

**Files:**
- Modify: `cpp/include/open/character.hpp` for per-character clocks and swing methods.
- Create: `cpp/src/character_combat.cpp` for the `Character` combat-method implementations.
- Modify: `cpp/include/open/combat_rules.hpp`, `cpp/src/combat_rules.cpp` for the swing result and uniform part sampler.
- Create: `cpp/tests/melee_swing_test.cpp`.
- Modify: `cpp/CMakeLists.txt` to build the new source and register `melee_swing_tests` / CTest name `melee_swing`.

**Interfaces:**
- Consumes Task 1 attack/range APIs, Task 2 profiles/formulas, and existing `spend`, `damage`, `record_use`, `record_title_progress`.
- Produce `SwingResult { float damage; bool nullified; std::optional<BodyPart> part; }` and `random_melee_part(std::mt19937_64 &rng) -> BodyPart`, using `std::uniform_int_distribution<int>(0, 5)` mapped to the six existing parts.
- Produce `Character::advance_combat_clocks(float seconds) -> bool`, `attack_cooldown_remaining() const -> float`, `defend_cooldown_remaining() const -> float`. Both clocks start ready at 0, decrease toward 0 only for finite nonnegative elapsed time, and persist through order changes. Invalid elapsed time returns false without mutation.
- Produce `Character::try_melee_swing(Character &defender, const Vec3 &self_position, const Vec3 &target_position, std::mt19937_64 &rng) -> std::optional<SwingResult>`.
- Empty result means no attempt: wrong/missing target order, dead actor/target, out of range, no usable attack or attack clock not ready. It spends/trains/damages nothing and consumes no defense clock. It does not advance elapsed time or regenerate.
- An attempt pays 8 if available, otherwise uses that swing's exhaustion flag. If defense is ready and usable, consume its clock on the attempt whether or not it wins the strict nullify test. A non-ready defense cannot nullify. Apply only positive full-hit damage to one sampled part, then start the attack cooldown; a nullify/zero hit still pays any payable stamina and starts cooldown. Use armour 0 at runtime.
- Grant attack XP using the exact captured attack profile only for positive damage; grant defense XP using its captured profile only on nullify. Compute the swing and both cooldowns from pre-award trained ranks. Record Wall only for a nullifying Block; record Duelist/Breaker once on a living-to-dead transition for the matching weapon basic. No title for unarmed/polearm kills is added. Do not acquire a new target after a kill.

- [x] **Write failing normal-swing tests:** `rank_one_unarmed_hit` uses two fresh characters at horizontal separation 1.5 with very different Y. Attack order target 2; a ready attempt reduces main HP by 0.04 and exactly one part by 0.04, leaves attacker stamina 92, adds 1 residual UnarmedCombat XP and leaves both levels unchanged. Attack cooldown is `1/0.214`; the ready defender's failed dodge also starts `1/0.214` but earns no XP. Immediately retrying is not an attempt. Wrong target, out-of-range, dead attacker and dead defender also make no attempt or mutations.
- [x] **Write failing fatigue/zero-hit tests:** `below_eight_is_per_swing` prepares 8, 7 and 0 stamina through existing spending APIs. Exactly 8 pays to 0 and does 0.04; 7 remains 7 and does 0.02; 0 remains 0 and does 0.02. Low-rank exhausted cooldown is clamped to 5. Recover stamina with existing regeneration, advance the clock, and assert the next payable swing restores normal damage/cooldown. `zero_damage_trains_nothing` uses freshly chosen rank-0 Boxing against a rank-0 advanced dodge so equality does not nullify: zero damage gives no attacker or defender XP, but spends stamina and starts both clocks.
- [x] **Write failing defense/clock tests:** `ready_defense_is_consumed_even_when_it_loses`, `nullify_awards_only_defender`, `independent_clocks_survive_order_changes`. Use a usable shield to nullify a rank-1 unarmed attack; assert no HP/part loss, attacker XP 0, Blocking XP +1, Wall progress +1, stamina spent and attack cooldown started. A trained unarmed dodge can nullify and earn Dodging XP without Wall progress. While defense is cooling down, a ready attacker lands a full hit. Cancelling, reissuing and retargeting do not reset either clock; two opposed attack orders have independent readiness. Advancing by the exact remaining duration makes a clock ready; negative/NaN/infinite time leaves it unchanged.
- [x] **Write failing part/kill tests:** `uniform_parts_are_seeded_and_cover_six` draws 6000 samples with seed 42, checks only the six valid parts, each count between 800 and 1200, and identical sequences for identical seeds. `one_damage_call_and_one_kill_credit` prepares each possible lethal body-part outcome just below the next hit using existing damage calls, uses a seed selecting that part, and asserts one HP subtraction, correct part clamp and exactly one matching Duelist/Breaker kill report. Further attempts on the dead target cannot add damage, XP or title progress. Include a limb hit whose overkill does not subtract from main HP twice.
- [ ] **Observe red:** configure with `OPEN_BUILD_GDEXTENSION=OFF`, build `melee_swing_tests`, and confirm failures identify the missing swing/clock interfaces.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*
- [x] **Implement the combat methods and sampler**, storing clocks on each `Character`, not in global/static variables or the attacker for both participants. Keep `character.cpp`'s existing sheet methods intact except the Task 1 order/death hooks. Do not add a scene loop or regeneration here.
- [x] **Verify green:** run `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build -R '^melee_swing$' --output-on-failure`, and full `ctest --test-dir cpp/build --output-on-failure`. Stop after this Task.

### Task 4: Coordinate target IDs, pursuit, combat state and the single simulation tick

**Files:**
- Create: `cpp/include/open/combat_session.hpp`, `cpp/src/combat_session.cpp`.
- Create: `cpp/tests/combat_session_test.cpp`.
- Modify: `cpp/CMakeLists.txt` to add the source and register `combat_session_tests` / CTest name `combat_session`.

**Interfaces:**
- Consumes Tasks 1–3; does not own or replace any `Character`.
- Produce `Combatant { Character *character; Vec3 position; }` and `CombatSession`, constructed explicitly with `CombatSession(std::uint64_t seed)` for a session-owned `std::mt19937_64`.
- Produce `issue_attack(std::span<const Combatant> actors, std::uint64_t actor_id, std::uint64_t target_id) -> bool`, `step(float seconds, std::span<const Combatant> actors) -> bool`, `in_combat(std::uint64_t id, std::span<const Combatant> actors) const -> bool`, and `velocity_at(std::uint64_t id, std::span<const Combatant> actors) const -> Vec3`.
- These spans are temporary snapshots; retain no character pointers or positions between calls. Reject an invalid roster (null pointers, duplicate IDs or non-finite positions) safely and atomically; return false/zero for invalid queries. Unknown/self/dead targets and dead actors cannot receive an attack; rejected commands preserve the old order. Same-faction targets are valid.
- `in_combat` is derived in C++ from living valid outgoing or incoming attack orders. Include pursuit, not just melee range. `velocity_at` resolves the current target and delegates to `attack_velocity_at`, or delegates to ordinary `Character::velocity_at` when there is no attack. A missing/dead target returns zero pending cancellation, never stale pursuit.
- A valid zero-duration `step` reconciles invalid/ended orders only; it does not swing, advance clocks or regenerate. A positive step: reconcile orders; advance **all** character clocks once; attempt at most one ready swing per ordered actor in roster order; reconcile again after deaths; regenerate each living actor once using the resulting C++ `in_combat`. Do not loop missed swings for a large frame or create a shared turn system. Negative/non-finite time is rejected before any mutation.
- Reconciliation cancels an order whose actor/target is dead, whose target is absent, whose usable attack is absent, or whose actor has both legs at zero and is out of range. Merely setting `can_move(false)` does not create a retaliatory order or forbid a usable in-range attack. Movement integration and collisions remain outside the sim; the supplied positions are observations, not simulated by this coordinator.

- [x] **Write failing order/state tests:** `resolve_ids_and_preserve_rejected_orders` accepts both Friendly and Enemy targets; rejects zero, self, absent and dead targets without replacing valid intent. `incoming_order_marks_passive_defender` asserts attacker and target are in combat during pursuit while the target has no attack order; a third bystander is not. A move/clear cancels the outgoing engagement. Target removal or death cancels pursuit on the next reconciliation, and the actor never selects the bystander.
- [x] **Write failing pursuit tests:** `pursuit_uses_live_positions` moves only the target's supplied snapshot and asserts velocity follows the new position, stops within 1.5, and remains horizontal. Standing defenders have zero velocity and no outgoing order. Both legs disabled out of range cancels pursuit; the same injury with a usable arm already in range can still swing. One disabled leg preserves the 2.25 speed from C++. Do not conflate the raw `can_move` switch with limb destruction.
- [x] **Write failing tick/regen tests:** `tick_is_single_owner` drains 10 stamina and 20 mana from attacker, target and bystander, with attacker/target out of range to avoid spending. One second yields +5 stamina/+1 mana for both engaged characters and +10/+4 for the bystander. After cancellation, former participants use +10/+4, clamped to maxima. Dead actors never regenerate or swing. `passive_defense_is_not_retaliation` lets an attack land and confirms defender still has no attack order and attacker HP is unchanged.
- [x] **Write failing safety/time tests:** `invalid_snapshots_and_time_are_atomic` checks duplicates, null, NaN positions and negative/NaN/infinite seconds against unchanged orders, clocks, pools and XP. Zero time may cancel a dead/missing-target order but never causes an opening swing or regen. `two_orders_use_two_clocks` issues reciprocal orders to two living actors, steps once and checks both can swing without a shared cooldown. Normal repeated small ticks respect cooldown rather than swinging every frame.
- [ ] **Observe red:** configure with `OPEN_BUILD_GDEXTENSION=OFF`, build `combat_session_tests`, and confirm missing coordinator interfaces are the reason for failure.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*
- [x] **Implement the coordinator**, keeping RNG/session coordination here and all swing/clock state on `Character`. Reuse the prior Tasks' capability, range and swing APIs; no duplicate damage math, faction rules, movement speed or regeneration rates.
- [x] **Verify green:** run `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build -R '^combat_session$' --output-on-failure`, then full `ctest --test-dir cpp/build --output-on-failure`. Stop after this Task.

### Task 5: Expose the combat coordinator through a thin GDExtension adapter

**Files:**
- Create: `extension/combat_bridge.hpp`, `extension/combat_bridge.cpp` for `OpenCombat : godot::RefCounted`.
- Modify: `extension/character_bridge.hpp` to grant `OpenCombat` narrowly scoped access to the existing private `sheet()` methods, without exposing raw pointers to GDScript.
- Modify: `extension/character_bridge.hpp`, `extension/character_bridge.cpp` for the read-only target getter.
- Modify: `extension/register_types.cpp`, `cpp/CMakeLists.txt` to register/build `OpenCombat` in the existing extension library.
- Create: `scripts/tests/combat_bridge_test.gd`.

**Interfaces:**
- Consumes Task 4 `CombatSession`/`Combatant` and existing one-time-configured `OpenCharacter` objects. Do not create copies of their sheets or a second source of HP/training.
- Produce bound Godot methods: `OpenCombat.add_character(sheet: OpenCharacter, position: Vector3) -> bool`, `remove_character(id: int) -> bool`, `set_position(id: int, position: Vector3) -> bool`, `issue_attack(actor_id: int, target_id: int) -> bool`, `step(seconds: float) -> bool`, `is_in_combat(id: int) -> bool`, `velocity_at(id: int) -> Vector3`.
- Produce `OpenCharacter.get_attack_target_id() -> int`, returning 0 for no order or an unconfigured sheet. Existing `set_destination`, `clear_order`, `regenerate`, damage and snapshot methods retain their signatures.
- `OpenCombat` holds strong `godot::Ref<OpenCharacter>` references and last supplied positions, building a temporary C++ roster for every coordinator call. Its default constructor seeds `CombatSession` with C++ randomness; seeded simulation tests remain in C++. No seed or training mutation API is needed in Godot.
- Reject null/unconfigured sheets, duplicate IDs, nonpositive signed IDs, unknown IDs and non-finite positions without changing state. Preserve full positive 64-bit character IDs; validate before converting to unsigned. Unregistered queries return false/zero. No exception crosses into Godot.
- Removal clears the removed actor's orders via the existing C++ command and runs zero-time reconciliation on survivors before releasing its reference. Retained references prevent dangling sheets; survivors never retain a removed target pointer. The adapter converts types/manages references only; it does not decide damage, reach, XP or `in_combat` itself.

- [x] **Write failing headless bridge checks:** instantiate the class through `ClassDB` with an existence guard so the initial failure is a clear missing-OpenCombat assertion. `registration_and_safe_defaults` covers an empty coordinator, null/unconfigured sheets, duplicate IDs, invalid positions, signed negative IDs and full-width ID `(1 << 32) + 1`. Assert failed calls do not disturb existing registration or intent.
- [x] **Observe red with the current extension:** first rebuild `libopen.so` and import using the Global Constraints' four commands, then run `godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd`. Confirm the missing class/method checks fail for the expected reason before implementing the adapter.
- [x] **Add failing end-to-end bridge checks:** register the existing player/target sheets at separation 1, issue Attack by ID, and assert both in-combat flags true, target getter 2 and C++ velocity zero in range. One `step(0.01)` reduces the same target sheet's HP by 0.04, changes exactly one part, and leaves its attack target 0. A new floor-style `sheet.set_destination(...)` removes the attack and restores move velocity. Retargeting after a swing must not bypass its cooldown.
- [x] **Add failing lifetime/safety checks:** issue an out-of-range attack, remove its target and assert attack target becomes 0 and in-combat false without damage. Kill an actor or target through the existing bridge and step; no further swings occur. Drop the caller's reference to a registered sheet and verify the coordinator still operates; removal and final coordinator release must not crash or leak objects at exit. Invalid time/position updates preserve prior state.
- [x] **Implement and bind only the adapter/getter**, register `OpenCombat` alongside `OpenCharacter`, and add its source to the existing `open` shared-library target. Keep all includes of Godot types in `extension/`. Do not wire the scene yet.
- [x] **Rebuild extension and import:** run the Global Constraints' four commands in order, including full CTest. Then run `godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd` and `godot --path . --headless --script res://scripts/tests/character_sheet_test.gd`. Expect passing checks; existing deliberate invalid-ID checks may log errors. Stop after this Task.

### Task 6: Add the Attack popup and wire picking, movement and the single scene clock

**Files:**
- Create: `scripts/attack_popup.gd` (`class_name AttackPopup`, a small `PanelContainer` containing one Attack button).
- Modify: `scripts/main.gd` for character/floor picking, popup routing, the combat adapter and its one physics tick.
- Modify: `scripts/player.gd`, `scripts/standing_dummy.gd` to consume coordinator velocity and remove unconditional scene-level out-of-combat regeneration.
- Create: `scripts/tests/melee_combat_test.gd`.
- Modify: `scripts/tests/character_visuals_test.gd` only if additional regression assertions are needed; preserve its existing floor-click helper coverage.
- Modify: `README.md` to document left-click character → Attack and floor click → move/cancel. No scene asset or model changes are needed; `scenes/main.tscn` already delegates world creation to `main.gd`.

**Interfaces:**
- Consumes Task 5 `OpenCombat` methods and `OpenCharacter.get_attack_target_id()`. Keep existing `Player.go_to(world_position: Vector3) -> void` forwarding to `sheet.set_destination`, and keep `main._try_move_to_click(screen_pos: Vector2) -> void` for existing floor tests.
- Produce `AttackPopup.attack_requested(target_id: int)` signal, `open_for_target(target_id: int, screen_position: Vector2) -> void`, `close() -> void`, and `covers(point: Vector2) -> bool`. It stores a pending ID only, starts hidden, clamps the small panel to the viewport, emits once on Attack and closes. Opening/dismissing it does not issue or cancel an order. Escape closes; an outside world click closes it and continues through world picking. Use STOP mouse filtering, not a full-screen click-blocking overlay.
- Add `combat: OpenCombat` to Player and StandingDummy. In `main._build_combat() -> void`, called after spawning all three configured actors, create `_combat`, register their sheets/positions, and assign that reference to each actor. Keep a presentation-only `_actors` list for publishing positions; do not duplicate faction/health/rank/order state there.
- Add `main._physics_process(delta: float) -> void` with `process_physics_priority = -10` so it publishes all actor positions and calls `_combat.step(delta)` once before actor physics callbacks. Actors use `combat.velocity_at(sheet.get_character_id())` for horizontal velocity, retain gravity, collision, facing and walk presentation, and do not also tick combat or regenerate. For standalone actor fixtures without a coordinator, preserve the existing ordinary sheet velocity and single `regenerate(delta, false)` fallback; the actual main scene always uses the coordinator path.
- Add `main._try_character_or_floor_click(screen_pos: Vector2) -> void`: raycast once against masks `1 | 2`, selecting the nearest collider. A living StandingDummy opens the action popup using its C++ character ID, regardless of Friendly/Enemy. Player/dead-character hits issue no order and do not fall through to the floor behind them. A world/floor hit routes to the existing floor helper, preserving the movement marker and successful move cancellation.
- Add `main._on_attack_requested(target_id: int) -> void`: send `_combat.issue_attack(_player.sheet.get_character_id(), target_id)` and hide the move marker only when accepted. C++ revalidates the pending target at confirmation; Godot does not calculate range, clocks, damage, XP or hit success.
- Extend `_popup_covers`/left-click input routing to protect both the existing CharacterPopup and AttackPopup. Preserve non-echo I toggling, sheet binding, dragging, scrolling and clicks outside the character sheet. Never treat Attack-button input as a floor click.

- [x] **Write failing real-input tests:** use the SceneTree `_check`/nonzero-exit style and viewport mouse press/release helpers from `character_popup_test.gd`. `living_dummy_click_opens_attack` projects the Friendly and Enemy collider centers through the real camera and sends clicks; assert a single tiny action popup appears with Attack, no move marker/order is issued, and existing intent is unchanged until confirmation. Assert player/dead-dummy clicks do not open it or order a move to the floor behind them.
- [x] **Write failing popup-isolation tests:** `attack_button_is_not_floor_input` confirms pressing Attack emits exactly the pending ID once, closes the popup and sets the C++ target without creating a move marker. `dismiss_and_stale_target` covers Escape, an outside floor click, selecting a different dummy before confirmation, and a target killed after opening but before Attack; stale confirmation must not start a fight. Check viewport-edge placement remains clickable and CharacterPopup clicks do not leak into the new picker.
- [x] **Write failing fight/cancel tests:** `approach_then_auto_swing` clicks Enemy → Attack from the original spawn, waits a bounded maximum of 240 physics frames for the first hit, and asserts initial approach, a stopped horizontal separation ≤ 1.5 without overlapping the dummy, and a first C++ HP decrease of 0.04 with exactly one part decreased. Assert player level unchanged, no target retaliation/order, target X/Z unchanged and `get_can_move()` still false. Continue through another cooldown to prove automatic repeat without extra clicks, then floor-click and assert target ID 0, movement/marker restored, both in-combat flags false and no later damage while moving away. Repeat the acceptance path for Friendly to prove there is no faction gate. Reset fixtures between independent cases.
- [x] **Write failing single-clock regressions:** `scene_uses_combat_regen_once` stops the world's automatic callbacks in a controlled fixture, positions actors out of range, drains mana with existing C++ APIs in a C++ fixture where necessary, and verifies the coordinator ownership in headless scene behavior without adding mutation bindings. For directly observable Godot coverage, start an in-range attack, note stamina immediately after its first swing, manually advance exactly one 0.1-second world tick and the actor callbacks with movement disabled, and assert stamina rises by 0.5, not 1.0 or 1.5; the still-cooling attack must not spend again. Assert default scene traversal publishes positions before combat and that passive defenders never gain outgoing attack intent. Keep the full quantitative mana/combat-state assertions in Task 4's C++ tests.
- [ ] **Observe red:** run the extension rebuild/CTest/import sequence from Global Constraints, then `godot --path . --headless --script res://scripts/tests/melee_combat_test.gd`; confirm missing popup/wiring behavior is the cause before implementing it.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*
- [x] **Implement the tiny popup and scene wiring**, reusing existing HUD/CanvasLayer patterns and collider layer 2. The picker must inspect a character before the floor, not do a floor-only ray first. Keep original dummy setup, colors, models, `can_move(false)` and existing camera/marker behavior. Do not add attack stats or range constants in GDScript.
- [x] **Verify C++ and import:** run `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON`, `cmake --build cpp/build -j"$(nproc)"`, `ctest --test-dir cpp/build --output-on-failure`, then `godot --path . --headless --import --quit` so the new `class_name` is registered.
- [x] **Run all Godot regressions:** run `godot --path . --headless --script res://scripts/tests/melee_combat_test.gd`, then the same command for `combat_bridge_test.gd`, `character_sheet_test.gd`, `character_popup_test.gd`, `character_visuals_test.gd` and `character_animation_test.gd`. Each must exit successfully with its checks passing; do not infer success from import alone.
- [ ] **Manually inspect and document controls:** verify both dummy popups, Attack click isolation, approach/stop, repeated automatic damage, floor cancellation, I-key sheet interaction, viewport-edge placement and unchanged standing dummy colors/positions. Starting damage is intentionally 0.04: a slowly changing rounded HP label is not a reason to raise weapon 8 or accelerate the clock. Record any visual checks not performed, update the README controls, review `git diff`/`git status` for accidental or generated changes, and stop without committing.  *(deliverables of this step are present and green; the step's own evidence was not retained - see the Verification record)*

---

## Verification record (2026-10-29)

Every Task's deliverables were audited line by line against this plan and
`docs/combat.md` sections 1-9, then re-verified with fresh runs on this machine:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON   # exit 0
cmake --build cpp/build -j"$(nproc)"                    # exit 0
ctest --test-dir cpp/build --output-on-failure          # 12/12 passed
godot --path . --headless --import --quit               # exit 0
godot --path . --headless --script res://scripts/tests/melee_combat_test.gd
godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd
godot --path . --headless --script res://scripts/tests/character_sheet_test.gd
godot --path . --headless --script res://scripts/tests/character_popup_test.gd
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
godot --path . --headless --script res://scripts/tests/character_animation_test.gd
                                                        # all six exit 0, "all checks passed"
cmake -S cpp -B <fresh> -DOPEN_BUILD_GDEXTENSION=OFF    # exit 0, 12/12 - open_sim is self-contained
```

All 27 Task 1-4 named fixtures and all Task 5/6 named checks exist and assert what
the checklists demand; `zero_damage_trains_nothing` is replaced by the superseding
rule's positive fixture `unlocked_specialisation_hits_and_trains_itself`. No Godot
header or godot-cpp dependency reaches `cpp/`. No GDScript computes reach, clocks,
damage, XP, hit success, `in_combat` or walk speed.

### Follow-up pass (same date)

Gaps the audit found, now closed:

- `OpenCombat::remove_character` released its retained reference before reconciling
  survivors; it now holds a local `Ref` across the zero-time reconciliation, which
  is the order this plan asks for.
- The bridge's `set_position` check in `combat_bridge_test.gd` passed either way; it
  now parks the mover past its destination so only a stored position can flip the
  velocity sign. Proven red by making `set_position` store nothing.
- New `combat_bridge_test.gd` check that two `OpenCombat` objects do not draw the
  same body-part sequence, pinning the plan's "seeds with C++ randomness". Proven
  red by a constant seed.
- The marker rule (hide **only** when the attack is accepted) had no coverage at
  all. `_marker_follows_attack_acceptance` now covers both branches; proven red by
  moving the hide outside the accepted branch.
- New `_every_actor_uses_the_coordinator` check that the scene registers all three
  actors with one coordinator. Proven red by skipping one registration. `_build_combat`
  no longer hands `combat` to an actor the session refused, which would otherwise
  freeze it at zero velocity instead of using the documented fixture fallback.
- `AttackPopup.open_for_target` opened a panel for id 0 or a negative id, giving an
  Attack button that could never order anything. Written red first, then fixed.
- `melee_combat_test.gd` no longer skips its "click outside the character sheet"
  check when no floor point is found, and reads its edge coordinates from the real
  viewport (`root.get_visible_rect()`) instead of a hardcoded 1280x720.
- New C++ fixture `exhausted_defence_pays_nothing`: a defender that could afford a
  cost spends nothing, and a tired defender's clock is never doubled. Proven red by
  charging the defence 8 stamina and by doubling it below 8.
- `melee_cooldown`'s self-comparing assertion in `combat_rules_test.cpp` is gone; the
  four rank rows already pin both band ends.
- `kill_title` no longer keeps a second copy of the Duelist/Breaker mapping; it asks
  `title_rule(...).skill`, so the rule table owns it.
- `combat_bridge.cpp` includes `class_db.hpp` explicitly.
- Extension seeding no longer uses `std::random_device`: this target is built
  `-fno-exceptions`, so a failing entropy source would terminate rather than throw.
  Seeding is now clock + ASLR + a counter, mixed by splitmix64, and cannot fail.

### Evidence this plan still cannot show

- The five "observe red" steps for Tasks 1-4 and 6 have no retained failure log.
  Only Task 5's red and green builds were logged (`.superpowers/sdd/2026-10-29-melee-
  combat/`). Their end state is verified green; that their tests were seen failing
  first is not.
- The Task 6 manual visual inspection was not performed. Not checked by eye: the two
  dummy popups at the project window size and a smaller one, Attack-click isolation
  and viewport-edge clickability felt by hand, approach/stop and repeated damage as
  motion, floor cancellation, I-key sheet feel, and that the standing dummies' colors
  and positions look unchanged. The automated checks assert the same behaviors
  numerically; they do not establish legibility or feel.
- Nothing was committed: this plan does not authorize it.
- Outside this plan, one repo drift was closed: `AGENTS.md` advertised a
  `GODOT_CPP_DIR` build option that `cpp/CMakeLists.txt` never implemented. It now
  does - a local godot-cpp checkout is used when the path is set, and a path with no
  `CMakeLists.txt` is a configure-time error. Verified against a real local
  checkout, against a bad path, and with a clean `-DOPEN_BUILD_GDEXTENSION=OFF`
  configure/build/ctest that needs no godot-cpp at all.
