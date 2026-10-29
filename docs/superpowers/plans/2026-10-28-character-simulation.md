# Character Simulation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Execute **native inline**, as requested, not with implementation subagents. Steps use checkbox (`- [ ]`) syntax for tracking. Wait for plan approval before implementation.

**Goal:** Implement the locked character rules in `docs/characters.md` as simple, efficient C++ and use the same character type for the player and standing NPCs.

**Architecture:** Keep `Character` as a small aggregate of attributes, skills/titles, vitals, equipment, and the existing move order. Use ordinary structs, enum-indexed arrays, immutable rule tables, and direct calls. Godot sends intent and presents the result; no event framework, ECS, inheritance hierarchy, generic modifier system, or persistence framework.

**Tech Stack:** C++20, existing CMake/CTest executable tests, Godot 4.7.2, godot-cpp `godot-4.5-stable`, existing GDExtension/GDScript. No new dependencies.

**Spec:** `docs/characters.md`, root `AGENTS.md`, and the human's clarification: **each point of body-part damage also removes one point from main HP**. Additional direction: keep everything as simple and efficient as possible.

## Global Constraints

- `cpp/` must not include Godot headers or link `godot-cpp`.
- C++ owns level, attributes, pools, skills, titles, blessing, limbs, equipment and movement rules. Godot draws and sends intent.
- Player and NPC are the same type. Do not create player/NPC subclasses.
- Level is `1`..`7`; multipliers are exactly `1, 2, 4, 8, 16, 32, 100`.
- Every attribute base starts at `1`. Pool bases are `100`; level/blessing do not multiply pools.
- Basic training starts at `1`, caps at `100`. Chosen advanced training starts at `0`, caps at `200`.
- A character has one blessing. The player supplies the choice; NPC creation chooses randomly in C++.
- Faction bridge values remain `0` friendly and `1` enemy.
- Arrive radius is `0.4`. Movement is horizontal; Y is ignored.
- Standing characters retain `set_can_move(false)` and never walk.
- C++ simulation changes need a failing test first, then the code, then `ctest` green.
- After any C++ or extension change, rebuild `libopen.so` before running Godot.
- New `class_name` GDScript needs a Godot import once (`godot --path . --headless --import --quit`).
- Do not commit `cpp/build/`, `extension/bin/`, `.godot/`, or `*.so`.
- Do not copy architecture/code from `Project_OPEN` or invent extra systems, UI, cameras or lore.
- Preserve existing user work. At planning time most source folders are untracked and README/ignore files have edits. Never stage everything or reset the tree. Ask before a commit would absorb unrelated work.

## Review Focus

1. Invalid levels/IDs and negative/non-finite input must not corrupt state or index outside arrays: Tasks 1–8.
2. Changing level, blessing or skill training must not accidentally multiply/refill pools: Tasks 2, 5, 7.
3. Advanced rank `0` must remain `0`, with frozen-basic fallback only for other jobs: Tasks 3, 7.
4. A hit must damage main and targeted HP once, apply death/limb restrictions, and retain equipment: Tasks 5–7.
5. Bridge changes must preserve click movement, models, labels and stationary NPCs, without putting rules in GDScript: Tasks 7–9.

---

## Scope and small implementation choices

Implement all locked sheet data and formulas. Do not build the combat, crafting, item catalog or other systems that will eventually use them.

**Proposed defaults to review, not new locked gameplay rules:**

- Use `float` for sheet quantities, matching existing code. Integer XP/counters stay integers. Calculations are small; avoid caches and allocation-heavy abstractions.
- Attribute bases must be finite and positive. No invented base cap or growth algorithm.
- When a maximum changes, preserve absolute current value, clamped to the new maximum. Do not refill, heal or revive through recalculation. Only creation starts full.
- Spending is all-or-nothing; insufficient resources leave the pool unchanged.
- Incoming damage is subtracted independently from main and targeted HP, then each clamps at zero. Thus head damage `30` gives head `0`, main HP `70` on a fresh character. This overkill interpretation is a proposal to review alongside the confirmed one-to-one rule.
- Disabled arms do not destroy/drop existing items. They restrict new carrying and held-item use. Armour remains equipped and may be unequipped as specified.
- Equipping into an occupied slot fails; callers explicitly unequip first. Do not build automatic swapping.
- Keep the current healthy walking speed `4.5` in C++. Apply only locked leg/death/movement-switch restrictions. No invented Agility/Athletics speed formula.
- The authoritative action caller reports skill successes and title progress once. No event bus, generic action dictionary, delivery ledger, or automatic skill training from resource spending.

**Explicitly deferred:** level earning, attribute earning, HP/limb healing, combat resolution, crit/drop rolls, item stats/weights/bonuses, backpack capacity, encumbrance capacity/slowdown formula, recruiting, squads, hunger/bleed, prosthetics and save/load.

Encumbrance is mentioned in the spec, but weights and the slowdown formula are not locked. Leave a documented future composition point in `Character::walk_speed()`; do not claim encumbrance is implemented or add a fake formula. Likewise, store backpack contents without inventing a capacity or requiring a bag to exist before any storage is possible.

## Current code and planned files

Current `Character` has faction/level, HP `100`, Strength `10`, four limb booleans, and a move order. `player.gd` bypasses it using `OpenMoveOrder` and `SPEED=4.5`. Standing characters use `OpenCharacter`; their label is built once. Existing CTest names are `move_order` and `character`; cached baseline passes 2/2, but this is not a fresh-build verification.

| File | Responsibility |
|---|---|
| Create `cpp/include/open/character_rules.hpp`, `cpp/src/character_rules.cpp` | Enums, immutable links/options/title/blessing tables and locked constants |
| Create `cpp/include/open/attributes.hpp`, `cpp/src/attributes.cpp` | Ten bases, level, blessing and effective attributes |
| Create `cpp/include/open/skills.hpp`, `cpp/src/skills.cpp` | Basic/advanced ranks and XP, selections, fourteen title counters |
| Create `cpp/include/open/vitals.hpp`, `cpp/src/vitals.cpp` | Main/stamina/mana pools, six body pools, spending, regen, damage and physical capabilities |
| Create `cpp/include/open/equipment.hpp`, `cpp/src/equipment.cpp` | Item references, twenty slots, backpack list and hand occupancy |
| Modify `cpp/include/open/character.hpp`, `cpp/src/character.cpp` | Compose the modules; expose intent/read APIs and movement |
| Modify `cpp/include/open/move_order.hpp`, `cpp/src/move_order.cpp` | Retain movement math; add clear/query and input checks |
| Create `cpp/tests/character_rules_test.cpp`, `attributes_test.cpp`, `skills_test.cpp`, `vitals_test.cpp`, `equipment_test.cpp` | Focused module tests using the existing executable style |
| Modify `cpp/tests/character_test.cpp`, `cpp/tests/move_order_test.cpp`, `cpp/CMakeLists.txt` | Integration tests, sources and CTest registration |
| Modify `extension/character_bridge.hpp`, `extension/character_bridge.cpp` | Safe creation/getters/commands; no simulation formulas |
| Modify `scripts/player.gd`, `scripts/standing_dummy.gd`, `scripts/main.gd` | Forward intent, present C++ state; retain existing models/camera |
| Create `scripts/tests/character_sheet_test.gd`; modify `scripts/tests/character_visuals_test.gd` | Bridge and existing scene regression tests |
| Modify `docs/characters.md`, `docs/README.md`, `README.md` | Accurate current state, approved damage rule and usage |

No separate services or managers. Keep title state alongside skill state because it is only fourteen counters plus skill modifiers. Keep body/pool health together so one-to-one damage is one operation, not synchronization between two subsystems.

## Test and commit convention

Each task follows RED → minimal implementation → GREEN → narrow commit. Use existing `expect_true`/`expect_vec` test style; add a finite-aware float comparison helper locally or in one tiny shared test header if repetition warrants it. Do not add a testing dependency. Check NaN explicitly: `fabs(NaN) > tolerance` does not detect it.

Configure a pure-C++ build, then use it throughout Tasks 1–6:

```bash
cmake -S cpp -B /tmp/opencode/open-character-build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build /tmp/opencode/open-character-build -j"$(nproc)"
ctest --test-dir /tmp/opencode/open-character-build --output-on-failure
```

Register new CTest cases `character_rules`, `attributes`, `skills`, `vitals`, `equipment`; their executables use `_tests`. Keep existing cases. Reconfigure when sources/tests change. Missing APIs can produce the initial RED compiler failure; subsequent behavior tests must fail for their named assertion, not an unrelated build/environment error.

The signatures below are the implementation contract. Checked getters throw `std::invalid_argument` for invalid enum IDs; bool mutations reject invalid inputs without mutation. Check numeric conversions before indexing. The bridge validates inputs and never lets an exception cross into Godot. Do not expose mutable module references from `Character`.

### Task 1: Add the locked rule tables

**Files:** Create `character_rules.hpp/.cpp`, `character_rules_test.cpp`; modify `cpp/CMakeLists.txt` and include the rules header from `character.hpp` to avoid a duplicate `Faction` declaration.

**Interfaces:** In namespace `open`, define `Attribute`, `BasicSkill`, `AdvancedSkill`, `Title`, `BodyPart`, `PoolKind`, `Slot`, and explicit `Faction { Friendly=0, Enemy=1 }`. Enums have `Count`; provide `bool valid(Enum)` overloads. Use `std::array` and `std::optional`, not dynamically registered rules.

- `float level_multiplier(int level)`; reject outside `1..7`.
- `SkillRule { const char* name; std::array<Attribute,2> links; int link_count; }` and `const SkillRule& skill_rule(BasicSkill)`.
- `AdvancedRule { const char* name; BasicSkill parent; }` and `const AdvancedRule& advanced_rule(AdvancedSkill)`.
- `TitleRule { const char* name; unsigned threshold; std::optional<BasicSkill> skill; }` and `const TitleRule& title_rule(Title)`.
- `BlessingRule { const char* god; const char* name; }` and `const BlessingRule& blessing_rule(Attribute)`.
- Constants for pool bases `100`, blessing `1.25`, skill title `1.1`, specialist target `1.25`, walking `4.5`, resource costs and regen. Keep existing `MoveOrder::kArriveRadius=0.4`.

- [x] **1. Write catalog tests:** exactly `10` attributes, `35` basics, `72` advanced options, `14` titles, `6` parts, `3` pools, `20` slots; every row/parent/link matches the tables below and the source spec. Test all seven multipliers and invalid IDs/levels. Check definition names are nonempty and no enum row is missing/duplicated.
- [x] **2. Observe RED:** configure and build/run `character_rules_tests`; record the missing API or wrong catalog assertion.
- [x] **3. Implement the fixed rule tables and register their source/test.** Preserve `open_sim` C++20, warnings and Godot-free linkage. Explicitly list sources; no runtime configuration loader.
- [x] **4. Verify GREEN:** build and run all headless tests; existing spike tests still pass.
- [x] **5. Review/stage only this task and commit:** `feat: define character sheet rules`.

#### Basic links and advanced options

Use PascalCase enum names without spaces. Preserve display spelling, including **ColosalSword** and **Profanity**. All advanced options inherit their parent's links. This table contains all 35 basics and 72 options:

| Basic | Linked attributes | Advanced options |
|---|---|---|
| OneHandedWeapons | Strength, Dexterity | Katana, Sword, Axe, Mace, Dagger |
| TwoHandedWeapons | Strength, Toughness | Greatsword, GreatAxe, ColosalSword, Warhammer |
| Polearms | Strength, Dexterity | Spear, Halberd |
| UnarmedCombat | Strength, Agility | Boxing, Taekwondo |
| Archery | Dexterity, Perception | Bow, Crossbow |
| Blocking | Strength, Toughness | SmallShields, BigShields, WallShields |
| Dodging | Agility | Footwork, AcrobatEvasion |
| Athletics | Agility, Constitution | Sprinting, DistanceRunning |
| Acrobatics | Agility, Dexterity | Climbing, Parkour |
| Swimming | Strength, Constitution | SpeedSwimming |
| Endurance | Constitution | None |
| Hunting | Perception, Luck | BigGameHunting, Trapping |
| Foraging | Perception, Intelligence | Herbalism, WildProvisioning |
| Medicine | Intelligence, Dexterity | Surgery, FieldAid |
| AnimalHandling | Charisma, Perception | Mounts, Beasts |
| Smithing | Strength, Dexterity | Weaponsmith, Armoursmith |
| Construction | Strength, Intelligence | Fortification, CivilEngineering |
| Cooking | Dexterity, Intelligence | FieldCooking, GourmetCooking |
| Alchemy | Intelligence, Profanity | Potions, Poisons |
| Research | Intelligence, Profanity | Engineering, ArcaneResearch |
| Mining | Strength, Constitution | OreExtraction, Quarrying |
| Farming | Constitution, Intelligence | Agriculture, Horticulture |
| Persuasion | Charisma | Diplomacy |
| Intimidation | Charisma, Strength | Terror |
| Trading | Charisma, Intelligence | Haggling |
| Leadership | Charisma | BattlefieldCommand, Governance |
| Spellcasting | Intelligence, Profanity | RitualCasting, CombatCasting |
| CastingSpeed | Agility, Profanity | ReactionCasting, SpellChaining |
| ManaControl | Constitution, Profanity | ManaConservation, Channeling |
| ElementalMagic | Profanity | Fire, Ice, Lightning, Earth, Light, Dark |
| Restoration | Intelligence, Profanity | Healing, Wards |
| Enchanting | Intelligence, Dexterity | WeaponEnchant, ArmourEnchant |
| Lockpicking | Dexterity, Perception | None |
| Thievery | Dexterity, Luck | Pickpocket, Burglary |
| Stealth | Agility, Perception | Infiltration, Assassination |

Attributes are Strength, Dexterity, Agility, Toughness, Constitution, Perception, Intelligence, Charisma, Luck, Profanity. Blessing rows in that same order: **Korr/Iron, Sile/Sure Hand, Ryn/Wind, Groth/Stone, Vel/Blood, Isk/Sight, Quen/Lore, Ael/Voice, Nyx/Fortune, Vex/Blasphemy**.

Body parts: Head, Torso, LeftArm, RightArm, LeftLeg, RightLeg. Pools: Hp, Stamina, Mana. Slots: Head, Chest, Legs, Feet, Hands, LeftHand, RightHand, Ring0..Ring9, Backpack, Belt, Collar. Gloves (`Hands`) are not held slots.

### Task 2: Implement attributes and blessing selection

**Files:** Create `attributes.hpp/.cpp`, `attributes_test.cpp`; modify `cpp/CMakeLists.txt`.

**Interfaces:** `Attributes(int level, Attribute blessing)` initializes all bases to `1`; validated constructor. Getters `int level() const`, `Attribute blessing() const`, `float base(Attribute) const`, `float effective(Attribute) const`. Mutations `bool set_level(int)`, `bool set_base(Attribute,float)`. `Attribute random_blessing(std::mt19937_64&)` uses an injected C++ generator and `std::uniform_int_distribution` over ten attributes; no global RNG or per-character clock reseeding.

- [x] **1. Write tests:** all bases `1`; level-1 Strength blessing gives Strength `1.25`, other attributes `1`; level 7 gives `125` and `100`. Strength base `2` gives `250` at level 7. Check each of ten blessings affects only its own attribute. Reject level `0/8`, invalid IDs, zero/negative/NaN/infinite bases and finite inputs that overflow effective values, without mutation. Same-seed generators produce the same valid sequence; do not assert a library-specific random sequence.
- [x] **2. Observe RED:** build/run `attributes_tests`.
- [x] **3. Implement:** canonical state is ten bases, one level, one blessing; calculate `base * level_multiplier * (blessed ? 1.25f : 1.0f)` on demand. No duplicated effective-value storage or blessing-switch mechanism.
- [x] **4. Verify GREEN:** build/run all headless tests.
- [x] **5. Narrow commit:** `feat: calculate character attributes and blessings`.

Representative assertions in `attributes_test.cpp`:

```cpp
open::Attributes a(7, open::Attribute::Strength);
expect_true("level-seven blessed strength", std::fabs(a.effective(open::Attribute::Strength) - 125.0f) < 0.0001f);
expect_true("level-seven unblessed dexterity", a.effective(open::Attribute::Dexterity) == 100.0f);
```

### Task 3: Implement training and one advanced choice per basic

**Files:** Create `skills.hpp/.cpp`, `skills_test.cpp`; modify `cpp/CMakeLists.txt`.

**Interfaces:** `Training { unsigned rank; unsigned xp; }`, where XP is residual toward the next point. `Skills` owns one basic training entry and one optional selected advanced track per basic.

- `Training basic(BasicSkill) const`; `std::optional<Training> advanced(AdvancedSkill) const`; `std::optional<AdvancedSkill> chosen(BasicSkill) const`.
- `Training resolve(BasicSkill,std::optional<AdvancedSkill>) const`: selected matching option uses its own track; any other valid option of that parent uses the basic. An option from another parent is invalid.
- `bool advance(AdvancedSkill)` requires parent `100` and no existing choice.
- `bool record_use(BasicSkill,std::optional<AdvancedSkill>,bool successful)` adds one XP only on success. No effect on failure or at cap. Return false for invalid IDs/context; true for valid no-ops.
- `unsigned basic_xp_to_next(unsigned rank)`, `advanced_xp_to_next(unsigned rank)` return `0` at cap, reject outside the valid rank range.

- [x] **1. Write tests:** all basics start `1/0`; seven uses give `1/7`, eighth gives `2/0`, next rank costs `16`. Pin thresholds basic `50→51=400`, `99→100=792`; advanced `0→1=8`, `99→100=792`, `100→101=4000`, `199→200=7960`. Basic cap takes `39,600` successes; advanced cap takes `637,608`. Capped uses discard XP. Before basic `100`, advancement fails; selected Katana starts `0`; Axe falls back to basic `100`; only Katana grows; no later Sword selection. Separate basics may each advance once; Fire excludes the other elements. Endurance/Lockpicking have no options. Failure and invalid parent/IDs do not mutate training.
- [x] **2. Observe RED:** build/run `skills_tests`.
- [x] **3. Implement integer thresholds and direct lookup.** No separate skill subclasses or unlock registry. At basic cap, the counter stays `100`; selected advanced overrides only its exact job.
- [x] **4. Verify GREEN:** build/run all headless tests, including both sides of advanced rank `100`.
- [x] **5. Narrow commit:** `feat: train skills and choose advanced specializations`.

```cpp
// After validation and cap checks:
unsigned basic_cost(unsigned n) { return 8u * n; }
unsigned advanced_cost(unsigned n) {
    return n < 100u ? 8u * std::max(n, 1u) : 40u * n;
}
// A successful use adds 1 residual XP. At threshold subtract the cost
// and increment rank. Failed/cancelled actions are successful=false.

// Representative test in skills_test.cpp:
open::Skills skills;
for (int i = 0; i < 8; ++i)
    skills.record_use(open::BasicSkill::Endurance, std::nullopt, true);
expect_true("eight uses raise endurance once", skills.basic(open::BasicSkill::Endurance).rank == 2);
expect_true("rank-up consumes residual XP", skills.basic(open::BasicSkill::Endurance).xp == 0);
```

### Task 4: Add the fourteen starter title counters

**Files:** Extend `skills.hpp/.cpp`, `skills_test.cpp`, and title definitions in `character_rules.cpp`.

**Interfaces:** `bool record_title_progress(Title,bool successful)`, `unsigned title_count(Title) const`, `bool has_title(Title) const`, `float title_multiplier(BasicSkill) const`, `float target_multiplier(bool target_is_dragon) const`. Saturate counters at threshold; unlocked titles never drop. The simulation action caller reports each relevant achievement once. For a one-handed dragon kill it reports both Duelist and Dragonslayer; those calls do not grant additional skill XP.

| Title | Achievement threshold | Effect |
|---|---|---|
| Dragonslayer | 100 dragon kills | Result against dragon ×1.25, not skill-wide |
| Duelist | 100 one-handed kills | OneHandedWeapons ×1.1 |
| Breaker | 100 two-handed kills | TwoHandedWeapons ×1.1 |
| Wall | 200 blocked hits | Blocking ×1.1 |
| Ghost | 100 undetected actions | Stealth ×1.1 |
| Huntsman | 100 harvested animals | Hunting ×1.1 |
| Sawbones | 50 successful treatments | Medicine ×1.1 |
| Hammerhand | 100 smithed items | Smithing ×1.1 |
| Mason | 10 finished structures | Construction ×1.1 |
| GreenThumb | 100 harvested farmed crops | Farming ×1.1 |
| Hexer | 100 cast spells | Spellcasting ×1.1 |
| Cutpurse | 50 stolen items | Thievery ×1.1 |
| SilverTongue | 50 successful persuasions | Persuasion ×1.1 |
| IronBack | 100 completed labour jobs | Endurance ×1.1 |

- [x] **1. Write table-driven tests:** each title absent at threshold-minus-one, present at threshold and forever afterward; failed progress does nothing. Check exact named-skill ×1.1 and unaffected skills ×1. Advanced inheritance is tested via its parent in Task 7. Dragonslayer is ×1.25 only against its target, never a second skill bonus. Invalid title IDs cannot mutate counters.
- [x] **2. Observe RED:** build/run `skills_tests` with the new assertions.
- [x] **3. Implement the small fixed counter array and modifiers.** Apply all held relevant title modifiers; keep specialist result modifiers separate. No dragons, quests, achievement dispatcher or other world systems are created.
- [x] **4. Verify GREEN:** build/run all headless tests.
- [x] **5. Narrow commit:** `feat: award permanent starter titles`.

### Task 5: Implement pools, body health, resource use and death

**Files:** Create `vitals.hpp/.cpp`, `vitals_test.cpp`; modify `cpp/CMakeLists.txt`.

**Interfaces:** `Pool { float current; float maximum; }`; `ResourceUse { Sprint, MeleeSwing, Labour, Swimming, Spell }`. `Vitals(const Attributes&,const Skills&)` initializes full pools/parts. `Pool pool(PoolKind) const`, `Pool part(BodyPart) const`, `bool recalculate(const Attributes&,const Skills&)`, `bool spend(ResourceUse,float quantity=1)`, `bool regenerate(float seconds,bool in_combat,const Attributes&)`, `bool damage(BodyPart,float amount)`, `bool alive() const`, `float leg_multiplier() const`, `bool one_hand() const`, `bool two_hands() const`, `bool can_carry() const`.

`recalculate` validates every derived maximum before committing and preserves current/clamps. Main HP maximum is `100 * Constitution base`; stamina `100 * trained basic Endurance * Constitution base`; mana `100 * trained basic ManaControl * average(Constitution base, Profanity base)`. Body maxima are head `0.25`, torso `0.5`, each arm/leg `0.4` times main maximum.

- [x] **1. Write tests:** fresh pools `100`, head `25`, torso `50`, limbs `40`. Constitution base `2`, Profanity `4`, Endurance rank `3`, ManaControl rank `2` gives HP/stamina/mana maxima `200/600/600`; body `50/100/80`. Level/blessing/title/advanced selection do not affect those maxima. Costs: sprint `10/s`, melee `8`, labour `5`, swimming `6/s`, spell `20`. Sprint/swim quantity means seconds, other uses require quantity `1`. Reject insufficient/invalid spend atomically; exact funds reach zero. At bases `1`, regen per second is stamina/mana `10/4` out of combat, `5/1` in combat; Intelligence `3`, Profanity `5` gives mana `16/4`. No HP/part regen; clamp at max; fractional ticks match whole ticks within tolerance when no clipping occurs. Reject negative/NaN/infinite duration/amount, invalid IDs and arithmetic overflow without mutation.

  Damage tests: head hit `10` gives head `15`, main `90`; head hit `30` gives head `0`, main `70`, dead. Torso hit `50` kills with main `50`. Two `40` leg hits leave main `20` and prevent walking; just one halves speed. On another fresh character, two `40` arm hits leave main `20` and prevent carrying/held use; one forbids two-handing only. Multiple hits can kill through main HP while head/torso stay positive. Zero damage is a no-op. Recalculation never refills or revives a zero part.
- [x] **2. Observe RED:** build/run `vitals_tests`.
- [x] **3. Implement fixed arrays and direct formulas.** `damage` validates first, subtracts the incoming amount from main and selected part once each, clamps at zero. Death is main/head/torso at zero. Spend/regen on dead characters fails. Do not expose arbitrary current-HP setters or add healing/limb severing.
- [x] **4. Verify GREEN:** build/run all headless tests.
- [x] **5. Narrow commit:** `feat: simulate character pools and body damage`.

Representative damage test (fresh fixture for this case):

```cpp
open::Attributes a(1, open::Attribute::Strength);
open::Skills skills;
open::Vitals v(a, skills);
expect_true("head hit accepted", v.damage(open::BodyPart::Head, 10.0f));
expect_true("head loses ten", v.part(open::BodyPart::Head).current == 15.0f);
expect_true("main HP also loses ten", v.pool(open::PoolKind::Hp).current == 90.0f);
```

### Task 6: Implement the small equipment container

**Files:** Create `equipment.hpp/.cpp`, `equipment_test.cpp`; modify `cpp/CMakeLists.txt`.

**Interfaces:** `using ItemId = std::uint64_t` (`0` invalid); `HandUse { None, OneHanded, TwoHanded }`; `ItemRef { ItemId id; std::bitset<20> allowed_slots; HandUse hand_use; }`. The future item system supplies these minimal compatibility facts; this is not a catalog. `Equipment` owns one vector of stored `ItemRef`s and a fixed twenty-slot array of optional `ItemRef`s. A two-handed item occupies **one stored primary entry**, and both held-slot queries report that same ID; never duplicate ownership.

- `bool add(ItemRef,bool can_carry)`; `bool equip(ItemId,Slot,bool one_hand,bool two_hands)`; `bool unequip(Slot)`.
- `std::optional<ItemId> equipped(Slot) const`; `std::vector<ItemId> backpack_items() const`; `bool held_usable(ItemId,bool one_hand,bool two_hands) const`.
- Linear ID lookup is sufficient for a character's small inventory; no hash registry, global item owner or generic container framework. Prevent duplicates by searching stored/equipped entries.

- [x] **1. Write tests:** all twenty slots empty; each armour/extra/ring slot works with compatible fictional items; gloves and held slots are distinct. Add/equip/unequip round trip; invalid/duplicate IDs, masks and occupied slots fail without losing the source item. Two-handed equip requires both hands free, both allowed by the record, and two usable arms. Both hand queries return one ID; unequipping either returns exactly one backpack entry. One-handed needs at least one usable arm (no invented same-side requirement). Disabled arms prevent incompatible use/new pickup, not item retention or unequipping. Existing armour remains on disabled limbs. Reject held types targeting non-held slots and invalid enum/mask inputs.
- [x] **2. Observe RED:** build/run `equipment_tests`.
- [x] **3. Implement atomic vector/slot transfers.** Normal one-handed items occupy one compatible held slot. Two-handed entries use a fixed primary held slot and reserve the other through query/occupancy logic. Before changing state, validate source, compatibility, destination and capabilities. Backpack contents remain stored when the bag slot changes; capacity/loaded-bag rules are deferred.
- [x] **4. Verify GREEN:** build/run all headless tests.
- [x] **5. Narrow commit:** `feat: store character equipment and backpack references`.

### Task 7: Compose Character and expose a minimal bridge

**Files:** Modify `character.hpp/.cpp`, `move_order.hpp/.cpp`, `character_test.cpp`, `move_order_test.cpp`, `extension/character_bridge.hpp/.cpp`; create `scripts/tests/character_sheet_test.gd`.

**C++ interfaces:**

- `CharacterConfig { std::uint64_t id; std::string name; Faction faction; int level; Attribute blessing; }`; `static std::optional<Character> create(const CharacterConfig&)`. Require nonzero ID, valid faction/level/blessing; allow empty name. No invalid public default character. Future world supplies unique IDs.
- Const getters: `std::uint64_t id()`, `const std::string& name()`, `Faction faction()`, `int level()`, `Attribute blessing()`, float `base(Attribute)`/`attribute(Attribute)`, `Pool pool(PoolKind)`/`body_part(BodyPart)`, `Training basic(BasicSkill)`, `std::optional<Training> advanced(AdvancedSkill)`, `std::optional<AdvancedSkill> chosen(BasicSkill)`, `bool has_title(Title)`, `unsigned title_count(Title)`, `std::optional<ItemId> equipped(Slot)`, `std::vector<ItemId> backpack_items()`. Also `float effective_skill(BasicSkill,std::optional<AdvancedSkill> = std::nullopt) const`, `float target_multiplier(bool target_is_dragon) const`.
- Forwarding mutations: `void set_name(std::string)`; bool `set_level(int)`, `set_base(Attribute,float)`, `advance(AdvancedSkill)`, `record_use(BasicSkill,std::optional<AdvancedSkill>,bool)`, `record_title_progress(Title,bool)`, `spend(ResourceUse,float=1)`, `regenerate(float,bool in_combat)`, `damage(BodyPart,float)`, `add_item(ItemRef)`, `equip(ItemId,Slot)`, `unequip(Slot)`.
- `bool alive() const`, `can_walk() const`, `can_carry() const`, `can_use_one_hand() const`, `can_use_two_hands() const`, `held_usable(ItemId) const`; `bool can_move() const` retains the separate raw switch, `void set_can_move(bool)`; `float walk_speed() const`.
- `bool set_destination(float,float,float)`, `void clear_order()`, `std::optional<Vec3> destination() const`, `Vec3 velocity_at(float,float,float) const`. Add clear/query/checked destination to `MoveOrder`; retain its reusable low-level speed argument, but Character computes speed itself.

**Formula:** resolved trained rank × average(linked effective attributes) × parent title multiplier. Target multiplier is separate for the future action result. No level multiplier is applied a second time.

**Minimal Godot bridge:** store `std::optional<open::Character>`. Replace configure with bool `configure(int64_t id,String name,int faction,int level,int blessing)` and bool `configure_npc(int64_t id,String name,int faction,int level,int64_t seed)`. NPC method uses injected/local C++ RNG and passes an explicit blessing into the same factory. One successful configuration per bridge. All signed IDs/enums are checked before conversion.

Preserve existing convenience getters; `get_strength` now returns effective Strength and limb booleans mean that part HP is above zero. Add only direct getters needed for inspection/testing: `is_configured`, `get_character_id`, `get_character_name`, `get_blessing`, `get_attribute(int)`, `get_attribute_base(int)`, `get_stamina`, `get_max_stamina`, `get_mana`, `get_max_mana`, `get_part_health(int)`, `get_part_max_health(int)`, `is_alive`, `can_walk`, `get_walk_speed`. These return the matching bool/int64/String/int/float values. Forward bool `apply_damage(int,float)`, `regenerate(float,bool)`, `set_destination(Vector3)` and void `clear_order()`. Character bridge velocity becomes `velocity_at(Vector3)` with **no speed parameter**.

No full dictionary serialization/snapshot API, item catalog bindings, generic action parser or editor framework. The C++ API implements the complete sheet; expose additional thin getters/commands later when a real UI needs them. Keep current `OpenMoveOrder` registered as a utility, but stop using it as the player sheet.

- [x] **1. Write aggregate and bridge tests.** Replace obsolete Strength `10` expectation with base `1`, effective `1.25` for a Strength-blessed level-1 character. Validate factory inputs, independent characters and empty names. At level 7 with Strength blessing, OneHandedWeapons rank `1` is `(125+100)/2=112.5`. Train parent `100`, select Katana: Katana `0`, Axe fallback `11,250`; eight Katana successes give `112.5`; Duelist gives `123.75` and fallback `12,375`. No squared level or duplicate title application. Selecting ManaControl advanced at parent `100` leaves mana max `10,000` with bases `1`. Endurance `1→2` raises stamina max `100→200` without refilling current. Invalid base changes that overflow any derived pool are atomic. Failed/cancelled uses/title progress do nothing. Death stops spending, regen, training, advancement, pickup/equip and walking, while read/unequip remain possible. Lethal injury retains equipment.

  Movement assertions: no order → zero; healthy speed `4.5`, one zero leg `2.25`, both zero/death/raw switch false → zero. Switch true cannot override legs/death. Exactly `0.4` distance stops; just above moves; Y is ignored; clear stops. Reject non-finite destinations without replacing the valid order; non-finite positions or negative/non-finite low-level speed yield zero.

  Bridge assertions: valid explicit blessing, repeatable seeded NPC assignment, bad IDs/faction/level/blessing rejected, failed creation leaves unconfigured, second successful configuration rejected, head hit `10` gives `15/90`, and velocity requires no Godot speed. Unconfigured reads return safe zero/false (`get_faction=-1`), mutation fails/no-ops, velocity zero; invalid getter IDs report an error and return zero rather than throwing across Godot.
- [x] **2. Observe RED:** build/run updated C++ tests, then rebuild/import and run `character_sheet_test.gd`; record named failures/missing APIs.
- [x] **3. Implement composition and bindings.** Validate base/level changes on a candidate copy before committing attributes/vitals. Recalculate maxima only when inputs change, including an Endurance/ManaControl rank gain. Return effective values on demand. Move walking `4.5` to C++; speed is alive + raw switch + leg multiplier only. Bind methods explicitly and retain model-independent simulation. Bridge creation and mutation must not put Godot types into `cpp/`.
- [x] **4. Verify GREEN:** run all headless C++ tests; configure/build `cpp/build` with extension ON, run CTest, import Godot, run the direct bridge test. The old scene call sites are updated immediately in Task 8; do not treat that temporary mismatch as a finished integration.
- [x] **5. Narrow commit:** `feat: compose C++ characters and expose the shared bridge`.

### Task 8: Migrate the existing scene, without adding UI

**Files:** Modify `scripts/player.gd`, `scripts/standing_dummy.gd`, `scripts/main.gd`, `scripts/tests/character_visuals_test.gd`; extend `scripts/tests/character_sheet_test.gd`.

**Interfaces:** `Player` owns `sheet: OpenCharacter`; setup fields `character_id:int=1`, `character_name:String=""`, `blessing:int=0`. `_ready` configures Friendly, level 1 using those inputs. Main supplies an exported `player_blessing` (default Strength `0` for the spike, explicitly replaceable by future character creation). No blessing-selection UI is added.

`StandingDummy.setup(faction:int,level:int,id:int,seed:int,name:String="")` calls `configure_npc`, checks success, then sets can_move false. Main uses IDs `2/3` and fixture seeds `2002/3003`; alternate-model tests supply other IDs. These are demonstration IDs, not a world ID generator.

- [x] **1. Write/migrate scene tests:** all three characters have configured sheets, valid blessings and distinct setup IDs; player uses the explicit selection. Preserve existing model, collision, relaxed pose, walk animation, faction, stationary-NPC and alternate-model assertions. Update every alternate setup call. Test on separate fresh fixtures that one leg hit `40` halves speed, two leave HP `20` and no walk; a head hit `25` leaves main `75` but kills/stops movement. Assert existing label updates when HP changes.
- [x] **2. Observe RED:** run the headless scene tests against old scripts; expect old configure/velocity signatures or missing player-sheet assertions to fail.
- [x] **3. Implement minimal presentation changes.** Player `go_to` forwards intent to its sheet; both character scripts obtain horizontal velocity from `sheet.velocity_at(position)`. Remove GDScript `SPEED=4.5`; use `get_walk_speed()` only to normalize animation, safely handling zero. Retain gravity, collision, actual visual velocity, turning, assets, camera and marker. Each character forwards regen `delta,false` once per physics tick; no combat system exists yet. Do not tick again from rendering or getters. Keep a reference to the standing label and refresh its text only when changed; formulas remain in C++.
- [x] **4. Verify GREEN:** rebuild extension, import, run both Godot test scripts and CTest. Run main scene headlessly for 30 frames without script errors. Manually launch/click if display access permits; report if only headless checks were possible.
- [x] **5. Narrow commit:** `feat: use C++ character sheets for the player and NPCs`.

### Task 9: Finish verification and update the documentation

**Files:** Update `docs/characters.md`, `docs/README.md`, `README.md`; add missing regression assertions to the owning existing tests only if coverage review finds a gap.

**Interfaces:** No new architecture or gameplay.

- [x] **1. Audit the coverage checklist below.** Add a named failing test for any uncovered requirement; do not duplicate tests or invent requirements just to create RED.
- [x] **2. Observe any new test fail for the right reason.** If coverage is already complete, record that instead of weakening green tests.
- [x] **3. Fix confirmed gaps and document actual behavior.** Record the approved damage coupling, new Current state, setup APIs, C++ ownership, single regen clock and deferred rules. Do not rewrite unrelated user documentation edits.
- [x] **4. Run the full verification commands below.** Inspect final diffs for generated files, Godot leakage, leftover GDScript gameplay formulas, unrelated changes, and accidental Open mechanics. Record manual/environment limitations honestly.
- [x] **5. Narrow commit:** `docs: document character simulation and verification`. Hand back the result; no merge/push unless separately requested.

## Final verification

```bash
# Pure C++ must work without fetching/linking Godot.
cmake -S cpp -B /tmp/opencode/open-character-final-build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build /tmp/opencode/open-character-final-build -j"$(nproc)"
ctest --test-dir /tmp/opencode/open-character-final-build --output-on-failure

# Rebuild the real extension before running Godot.
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
godot --path . --headless --script scripts/tests/character_sheet_test.gd
godot --path . --headless --script scripts/tests/character_visuals_test.gd
godot --path . --headless --quit-after 30
```

Expected: seven CTest cases pass in both modes; both Godot scripts exit `0`; main scene has no script/parser errors. Then use `./scripts/build_and_run.sh` for manual click/movement/model/label checks if available. Cached pre-change tests are not final evidence.

## Coverage and future integration checklist

| Spec requirement | Tasks / boundary |
|---|---|
| Identity/name/faction, same player/NPC type | 1, 7–8 |
| Ten attributes, exact seven level multipliers and ten blessings | 1–2, 7 |
| All pool formulas, costs, regen and clamping | 5, 7–8 |
| All 35 basic links, averaged effective attributes | 1, 3, 7 |
| All 72 options, one/parent, frozen fallback, advanced rank zero | 1, 3, 7 |
| Exact XP curves, success only, caps | 3, 7 |
| All 14 titles, inherited skill bonuses, target-only Dragonslayer | 1, 4, 7 |
| Six body pools, death and one-to-one main damage | 5, 7–8 |
| Leg slowdown, arm capabilities, separate movement switch | 5–8 |
| Twenty equipment slots, backpack references, retained injured gear | 1, 6–7 |
| Weight/encumbrance/catalog/bonuses | Gated on missing rules; not claimed implemented |
| Move order, horizontal movement, arrival 0.4, C++ speed | 7–8 |
| Player choice / C++ random NPC blessing | 2, 7–8; creation UI deferred |
| Healing, recruiting, squads, hunger/bleed, prosthetics | Not built |

**Future integrations stay direct:** combat/crafting/magic ask `effective_skill`, spend resources when appropriate, call `damage` with final damage, and report successful use/title progress once. Combat applies the specialist target multiplier to the result once, not again to skill training. The future item system supplies real item references/compatibility and later weights; it does not overwrite attribute bases with equipment bonuses. The future world owns IDs, RNG and the update clock, replacing Godot's regen-forwarding call rather than adding a second tick. Save/load can later serialize canonical state using versioned stable names; do not build it now or promise that raw enum ordinals are a save format.

## Review and handoff

- [x] Read the spec, repository rules, current C++, bridge, scene scripts and tests.
- [x] Record the confirmed one-to-one damage coupling.
- [x] Simplify the design after the request to avoid overcomplication.
- [x] Map every locked section, exact catalog counts, tests and future boundaries.
- [ ] Human reviews proposed defaults, especially overkill, preserving current values on max changes, and item retention after injury.
- [x] After approval, implemented **native inline** using `executing-plans`, task by task (Tasks 1-9, commits 71a1df5..94bd7e9, ledger in .superpowers/sdd/2026-10-28-character-simulation/ before it was deleted).
