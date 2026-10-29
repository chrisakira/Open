# Characters

A character is an ordered actor: you give orders, they carry them out.

Player and NPC are the same type. The player is the character you currently issue orders to.

- **Current** — true in `cpp/` today.
- **Locked** — decided for this file. Still not a build order.
- **Open** — do not implement until the human locks it.

We define the sheet in this order, and we do not skip ahead:

1. Attributes (locked)
2. Basic skills (locked)
3. Titles (locked starters)
4. Advanced skills (locked)
5. Blessings (locked)
6. Limbs (locked)
7. Equipment (locked)

Code and tests win if they disagree with an older paragraph. Do not invent races, towns, or lore here.

## Current

The sheet now exists in `cpp/`: `Attributes`, `Skills`, `Vitals`, `Equipment` and
`MoveOrder`, composed by `Character` (`cpp/include/open/character.hpp`). Player and
NPC are one type; Godot sends intent and draws the result.

| Field | Now |
|---|---|
| id / name / faction | set at creation. `Character::create` refuses a zero id, a bad faction, a level outside `1..7` and an unknown blessing. The name may be empty. |
| level | `1`..`7`, multipliers exactly `1, 2, 4, 8, 16, 32, 100`; attributes only, never pools |
| attributes | ten bases, all `1` at creation. Effective = base × level multiplier × blessing. |
| blessing | one per character. The player's is supplied (`player_blessing` in `scripts/main.gd`, default Strength); an NPC's is drawn in C++ from a caller-provided seed. |
| pools | HP `100 × Constitution base`; stamina `100 × Endurance rank × Constitution base`; mana `100 × Mana Control rank × average(Constitution base, Profanity base)`. All start full. |
| body parts | head `25%`, torso `50%`, each limb `40%` of the main HP maximum. |
| damage | implemented as locked: one hit subtracts once from main HP and once from the named part, each clamped at `0`. Overkill on the part is not charged to main HP again. |
| skills | 35 basics start at rank `1`; one successful use adds one residual XP; caps `100` basic / `200` advanced; one specialization per basic, chosen at basic `100`, other jobs keep the frozen `100`. |
| titles | fourteen counters, saturating at threshold, never dropped; `×1.1` on the named skill, Dragonslayer `×1.25` on the target result only. |
| equipment | twenty slots plus backpack references. A two-handed item is stored once and the other held slot reports the same id. |
| `can_move` | still a separate switch; legs at `0` or death stop walking even when it is `true`. |
| walk speed | `4.5 × leg multiplier`, computed in C++ (`Character::walk_speed`). No GDScript speed constant. |
| regen | one clock: each character forwards `regenerate(delta, false)` to its sheet once per physics tick. |

Standing characters still call `set_can_move(false)`, so they never walk. Arrive
radius is `MoveOrder::kArriveRadius = 0.4`; movement is horizontal, Y is ignored.

The bridge is `OpenCharacter` (`extension/character_bridge.cpp`):
`configure(id, name, faction, level, blessing)` for the player and
`configure_npc(id, name, faction, level, seed)` for an NPC, one successful
configuration per object. Reads answer `0` / `false` / `-1` and bad ids report an
error instead of throwing across Godot. `velocity_at(position)` takes no speed —
the sheet owns it.

Not built: earning levels or attribute bases, healing, combat resolution, crit and
drop rolls, item stats/weights/bonuses, backpack capacity, the encumbrance formula,
recruiting, squads, hunger/bleed, prosthetics, save/load. `OpenMoveOrder` stays
registered as a utility, but it is no longer the player's sheet.

## 1. Attributes — locked

Every character has a main level, ten attributes, and three pools.

These are attributes and pools, not skills. Skills come next and must not duplicate these names.

### Main level
An integer on every character. Same field as the spike `level`. Valid values are `1`..`7`. `7` is the maximum.

Level is a multiplier on the ten attributes only. It does not multiply pools (HP, Stamina, Mana) and it does not multiply itself. Lore for why the curve jumps at 7 comes later; the numbers are locked now.

| Level | Multiplier |
|---|---|
| 1 | ×1 |
| 2 | ×2 |
| 3 | ×4 |
| 4 | ×8 |
| 5 | ×16 |
| 6 | ×32 |
| 7 | ×100 |

Effective attribute = base × level multiplier × blessing (blessing is 1.25 on the one blessed attribute, else 1).

Pools, skills, titles, and blessings are not multiplied by level. A skill may be multiplied by an attribute (that attribute already includes level).

### Attributes

| Attribute | Meaning |
|---|---|
| Strength | Raw physical power |
| Dexterity | Precision and coordination |
| Agility | Movement speed and reflexes |
| Toughness | Resistance to physical damage |
| Constitution | Health, stamina, and recovery. Multiplies the Endurance skill. |
| Perception | Environmental and combat awareness |
| Intelligence | Learning, knowledge, and magical understanding |
| Charisma | Leadership, persuasion, and social influence |
| Luck | Chance of drops and critical strikes |
| Profanity | Used for magic |

Spike `strength` is this Strength. The other nine do not exist in code yet.

Every attribute **base** starts at `1` (before level and blessing).

### Pools — locked

Each pool has a current value and a max. Current starts at max on creation. When a maximum later grows or shrinks, keep the current value and clamp it to the new maximum. Do not refill, heal, or revive by recalculating.

Level does not multiply pools. Pool maxima use attribute **bases**, not effective attributes, so a level 7 with the same bases has the same bar as a level 1. Skills and combat still use effective attributes.

| Pool | Max |
|---|---|
| HP | `hp_base` × Constitution base |
| Stamina | `stamina_base` × trained Endurance × Constitution base |
| Mana | `mana_base` × trained Mana Control × average(Constitution base, Profanity base) |

Stamina and mana can still grow at the same level by training Endurance and Mana Control. HP has no trained health skill; it only moves when Constitution **base** changes.

`hp_base`, `stamina_base`, and `mana_base` are all `100`.

Spend
- Stamina: sprinting, melee swings, labour, swimming
- Mana: spells

Regen
- Out of combat: both regen. Stamina from Constitution. Mana from the average of Intelligence and Profanity, slower than stamina.
- In combat: stamina regen reduced, mana regen reduced more.

Spend (placeholders, balance later)
- Sprint: 10 stamina per second
- Melee swing: 8 stamina
- Labour action: 5 stamina
- Swimming: 6 stamina per second
- Spell (baseline): 20 mana

Regen per second (placeholders, balance later). Uses attribute **bases**, so level does not change regen.
- Stamina out of combat: 10 × Constitution base
- Mana out of combat: 4 × average(Intelligence base, Profanity base)
- Stamina in combat: half of out-of-combat
- Mana in combat: one quarter of out-of-combat

At start (bases 1): stamina 10/s out of combat (~10s to full 100), mana 4/s (~25s to full 100). Cannot spend below 0.

Main HP at `0` = dead. Head or torso at `0` is also death even if main HP is above `0`.

## 2. Basic skills — locked

Each skill has its own trained counter. That counter grows by use. Level does not increase it.

Effective skill = trained counter × link.

- One linked attribute: link = that effective attribute. Endurance is this.
- Two or more: link = average of those effective attributes.

Do not multiply two effective attributes together. Each already contains the level multiplier; a product would square level (level 7 would be ×10,000).

Luck still rolls drops and crits on its own. It is also on Hunting and Thievery as a skill link.

### Links — locked

Combat

| Skill | Attributes |
|---|---|
| One-Handed Weapons | Strength, Dexterity |
| Two-Handed Weapons | Strength, Toughness |
| Polearms | Strength, Dexterity |
| Unarmed Combat | Strength, Agility |
| Archery | Dexterity, Perception |
| Blocking | Strength, Toughness |
| Dodging | Agility |

Physical

| Skill | Attributes |
|---|---|
| Athletics | Agility, Constitution |
| Acrobatics | Agility, Dexterity |
| Swimming | Strength, Constitution |
| Endurance | Constitution |

Survival

| Skill | Attributes |
|---|---|
| Hunting | Perception, Luck |
| Foraging | Perception, Intelligence |
| Medicine | Intelligence, Dexterity |
| Animal Handling | Charisma, Perception |

Crafting and labour

| Skill | Attributes |
|---|---|
| Smithing | Strength, Dexterity |
| Construction | Strength, Intelligence |
| Cooking | Dexterity, Intelligence |
| Alchemy | Intelligence, Profanity |
| Research | Intelligence, Profanity |
| Mining | Strength, Constitution |
| Farming | Constitution, Intelligence |

Social

| Skill | Attributes |
|---|---|
| Persuasion | Charisma |
| Intimidation | Charisma, Strength |
| Trading | Charisma, Intelligence |
| Leadership | Charisma |

Magic

| Skill | Attributes |
|---|---|
| Spellcasting | Intelligence, Profanity |
| Casting Speed | Agility, Profanity |
| Mana Control | Constitution, Profanity |
| Elemental Magic | Profanity |
| Restoration | Intelligence, Profanity |
| Enchanting | Intelligence, Dexterity |

Utility

| Skill | Attributes |
|---|---|
| Lockpicking | Dexterity, Perception |
| Thievery | Dexterity, Luck |
| Stealth | Agility, Perception |

Every attribute appears at least once. Strength, Dexterity, Agility, Intelligence, Profanity show up often because they are the common verbs. Toughness is only on Two-Handed and Blocking — that is intentional: it is soak, not a craft stat.

Trained counter is `1`..`100`. Every basic skill starts at 1. Cap is 100. Reaching 100 unlocks that skill’s advanced options.

XP: diminishing returns. A successful use always grants some progress, but each next point costs more than the last. Hitting 100 on a basic skill is pretty hard.

When an advanced skill unlocks it starts at 0 and the difficulty curve **resets** — early advanced points are as easy as early basic points. Going from there to 200 is really difficult, harder than 100 was.

Each successful use grants 1 XP toward the next trained point. Failed or cancelled actions grant 0.

XP to raise trained counter from `n` to `n+1` (placeholders, balance later):

- Basic skill (`n` is 1..99): `8 * n`
- Advanced skill (`n` is 0..199): `8 * max(n, 1)` while `n < 100`; `40 * n` while `n >= 100`

Examples: basic 1→2 costs 8; 50→51 costs 400; 99→100 costs 792 (~39,600 XP to cap). Advanced 0→1 costs 8 (curve reset). Advanced 100→101 costs 4000; 199→200 costs 7960.

## 3. Titles — locked (starters)

A title is an earned modifier on skills. It is not a skill, not an attribute, and not multiplied by level.

Unlock: a counted achievement. Until the count is hit, the title is absent. After that it stays on the character.

A character may hold many titles. They all apply. Titles never drop.

Dragonslayer is the pattern: kill 100 of a kind → extra damage against that kind. Dragons are not in the world yet; the row stays as the template.

### Starter list — locked

| Title | Unlock | Skill impact |
|---|---|---|
| Dragonslayer | Kill 100 dragons | Extra damage vs dragons (when that foe exists) |
| Duelist | 100 kills with one-handed weapons | One-Handed Weapons |
| Breaker | 100 kills with two-handed weapons | Two-Handed Weapons |
| Wall | Block 200 hits | Blocking |
| Ghost | 100 actions while undetected | Stealth |
| Huntsman | Harvest 100 animals | Hunting |
| Sawbones | 50 successful treatments | Medicine |
| Hammerhand | Smith 100 items | Smithing |
| Mason | Finish 10 structures | Construction |
| Green Thumb | Harvest 100 farmed crops | Farming |
| Hexer | Cast 100 spells | Spellcasting |
| Cutpurse | Steal 50 items | Thievery |
| Silver Tongue | 50 successful persuasions | Persuasion |
| Iron Back | Complete 100 labour jobs | Endurance |

Specialist titles (Dragonslayer) modify what the skill *does* against a target: ×1.25 vs that target. The others modify the named skill: ×1.1.

Extra titles later: Open.

## 4. Advanced skills — locked

An advanced skill is a specialization of one basic skill. A character may advance a given basic **once**, into **one** of its options.

On advance:
- The basic skill stops growing. Its trained counter is frozen at 100.
- The advanced skill **overrides** the basic for that job. You use Katana, not One-Handed, when swinging a katana.
- Other weapons still under that basic (axes if you took Katana) use the frozen basic (100) forever.
- The advanced skill has its own trained counter, `0`..`200`. It starts at 0 and grows by use up to 200. The frozen basic stays at 100 for anything not covered by the advanced skill.

Attribute links inherit from the parent basic unless a row says otherwise. Level still does not multiply skills. Titles that named the parent apply to the advanced skill.

Advance is available only at basic 100. There is no earlier spec.

### Options — locked

From One-Handed Weapons (Strength, Dexterity)
- Katana — curved single-edge blades, cuts and draw-cuts
- Sword — straight one-handed blades
- Axe — one-handed chopping weapons
- Mace — one-handed blunt weapons
- Dagger — short blades, close stabbing

From Two-Handed Weapons (Strength, Toughness)
- Greatsword — long two-handed blades
- GreatAxe — two-handed chopping weapons
- ColosalSword — oversized two-handed blade
- Warhammer — two-handed blunt weapons

From Polearms (Strength, Dexterity)
- Spear — thrusting reach weapons
- Halberd — poleaxe, hook and chop

From Unarmed Combat (Strength, Agility)
- Boxing — fists, punches, guards
- Taekwondo — kicks and striking with the legs

From Archery (Dexterity, Perception)
- Bow — drawn bows
- Crossbow — mechanical bows

From Blocking (Strength, Toughness)
- Small Shields — light, fast blocks
- Big Shields — standard coverage
- Wall Shields — heavy tower coverage

From Elemental Magic (Profanity)
- Fire — heat and burning
- Ice — cold and slowing
- Lightning — shock and burst
- Earth — stone and force
- Light — radiance and holy force
- Dark — shadow and unholy force
Picking one is the only element. That is the spec tax.

From Restoration (Intelligence, Profanity)
- Healing — restore HP
- Wards — protective magic

From Enchanting (Intelligence, Dexterity)
- Weapon enchant — imbue weapons
- Armour enchant — imbue armour

From Smithing (Strength, Dexterity)
- Weaponsmith — make weapons
- Armoursmith — make armour

From Alchemy (Intelligence, Profanity)
- Potions — beneficial draughts
- Poisons — harmful draughts

From Medicine (Intelligence, Dexterity)
- Surgery — serious injury and limb work
- Field aid — quick treatment in the field

From Stealth (Agility, Perception)
- Infiltration — moving unseen into places
- Assassination — killing from stealth

From Thievery (Dexterity, Luck)
- Pickpocket — stealing from people
- Burglary — stealing from places

From Animal Handling (Charisma, Perception)
- Mounts — riding and training mounts
- Beasts — taming and directing animals

From Dodging (Agility)
- Footwork — sidesteps, positioning, avoiding attacks
- Acrobat Evasion — rolls, leaps, evasive maneuvers

From Athletics (Agility, Constitution)
- Sprinting — max speed and acceleration
- Distance Running — efficient long-distance travel

From Acrobatics (Agility, Dexterity)
- Climbing — walls, cliffs, obstacles
- Parkour — vaulting, complex terrain

From Swimming (Strength, Constitution)
- Speed Swimming — fast surface movement

Endurance stays basic-only. No Combat Conditioning / Labour Conditioning.

From Hunting (Perception, Luck)
- Big Game Hunting — large and dangerous creatures
- Trapping — capturing animals with traps
Monster Hunting is later, if monsters get their own harvest/combat.

From Foraging (Perception, Intelligence)
- Herbalism — herbs and medicinal plants
- Wild Provisioning — edible wild resources

From Mining (Strength, Constitution)
- Ore Extraction — metals and rare minerals
- Quarrying — stone and construction materials

From Farming (Constitution, Intelligence)
- Agriculture — large-scale food crops
- Horticulture — rare plants and magical ingredients

From Construction (Strength, Intelligence)
- Fortification — walls, defenses, military structures
- Civil Engineering — roads, bridges, infrastructure

From Cooking (Dexterity, Intelligence)
- Field Cooking — efficient meals and long-lasting rations
- Gourmet Cooking — high-quality food, stronger bonuses

From Research (Intelligence, Profanity)
- Engineering — mechanical inventions and technologies
- Arcane Research — magical discoveries, artifacts, knowledge

From Persuasion (Charisma)
- Diplomacy — negotiations, peaceful resolutions, faction relations

From Intimidation (Charisma, Strength)
- Terror — frightening enemies, weakening morale

From Trading (Charisma, Intelligence)
- Haggling — individual buy/sell prices

From Leadership (Charisma)
- Battlefield Command — squad coordination and combat orders
- Governance — settlement management, morale, administration

From Spellcasting (Intelligence, Profanity)
- Ritual Casting — complex spells that need preparation
- Combat Casting — maintaining spells while threatened

From Casting Speed (Agility, Profanity)
- Reaction Casting — casting quickly in response to events
- Spell Chaining — successive spells with reduced delay

From Mana Control (Constitution, Profanity)
- Mana Conservation — reducing mana consumption
- Channeling — sustained magical effects

No advanced: Lockpicking. Endurance is basic-only on purpose.

## 5. Blessings — locked

A blessing is an extra growth factor on **one** attribute. It is not a skill, not a title, and not a pool.

There is one blessing per attribute, and one God per attribute. The blessing comes from that God.

A character has **one** blessing. The player picks it at game start. Every other character is assigned one at random from the ten.

Effective attribute = base × level multiplier × blessing.

No blessing: blessing = 1 (should not happen on a living character once assigned). Blessed: ×1.25 on that attribute.

Blessings do not apply to pools (pools use attribute bases). Skills use effective attributes, so a Strength blessing also shows up in Strength-linked skills.

### Gods and blessings — locked

| Attribute | God | Blessing |
|---|---|---|
| Strength | Korr | Iron |
| Dexterity | Sile | Sure Hand |
| Agility | Ryn | Wind |
| Toughness | Groth | Stone |
| Constitution | Vel | Blood |
| Perception | Isk | Sight |
| Intelligence | Quen | Lore |
| Charisma | Ael | Voice |
| Luck | Nyx | Fortune |
| Profanity | Vex | Blasphemy |

## 6. Limbs — locked

Every character has a main HP pool **and** a health pool on each body part.

Body parts:
- Head
- Torso
- Left arm, right arm, left leg, right leg

Each part has its own current HP and max HP, as a fraction of main HP max (which is `100` × Constitution base):

| Part | Max |
|---|---|
| Head | 25% of main HP max |
| Torso | 50% of main HP max |
| Each limb | 40% of main HP max |

Head or torso at `0` = dead, even if main HP is still above `0`.

A limb at `0` does not kill. It applies a debuff:

| Limb at 0 | Debuff |
|---|---|
| One leg | Walk at half speed |
| Both legs | Cannot walk |
| One arm | One-handed only; cannot two-hand |
| Both arms | Cannot carry; cannot use a held weapon |

`can_move` is still a separate switch. Legs at 0 override walking even if `can_move` is true.

Main HP at `0` = dead.

Damage: each hit names one body part. Subtract that amount from the part **and** from main HP, then clamp each at 0. Overkill on the part does not take extra from main. Example: fresh character, head hit 30 → head 0, main 70, dead because the head is 0. Limb hits can also empty main HP while head and torso are still up.

## 7. Equipment — locked

The character owns equipped slots plus what the backpack holds. What an item *is* (damage, armour value, weight) belongs to items, not this file.

### Armour
- Head
- Chest
- Legs
- Feet
- Hands

Hands armour is gloves. It is not the held weapon.

### Held
- Left hand
- Right hand

Two-handed use needs two arms that are not at 0 HP. One arm at 0: one-handed only. Both arms at 0: cannot use a held weapon.

### Extra
- Rings: one per finger (10)
- Backpack: one. This is the bag.
- Belt: one
- Collar: one

Encumbrance comes from carried weight vs Strength and slows walk. Equipped armour on a limb at 0 HP stays on. It can be unequipped. It does not drop. Held items stay too; a disabled arm only blocks new pickup and held-item **use**.

Item catalog, weights, and slot bonuses: Open.

## Also on the sheet (not this item)

- id, name (name may be empty)
- faction `Friendly` / `Enemy`
- `can_move` and the current move order
- hunger / bleed: parked from earlier, not attributes

## Open (not attributes)

Death: main HP, head, or torso at `0`. Not numbered here (other systems): recruiting, combat math, item catalog/weights, squads, prosthetics, neutral faction, hunger/bleed.

## Ownership

C++ owns level, attributes, pools, and the rest of the sheet. Godot draws and sends intent. GDScript must not become the source of truth for attributes, pools, or level.
