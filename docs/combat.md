# Combat

You pick a target. The ordered character walks there and fights. Swings are automatic once they are in range. You do not click each attack.

Player and NPC use the same rules. The player is the character you currently issue orders to.

- **Current** — true in `cpp/` today.
- **Locked** — decided for this file. Built in `cpp/` unless a sentence is marked Open.
- **Proposed** — written so we can lock it. Not a build order.
- **Open** — do not implement until the human locks it.

We define combat in this order, and we do not skip ahead:

1. Orders (locked)
2. Range (locked)
3. The swing (locked)
4. Attack speed (locked)
5. Damage (locked)
6. Defense (locked)
7. Body part (locked)
8. Combat state (locked)
9. XP from fighting (locked)
10. Later systems (ranged, magic, groups)

Sheet numbers live in `docs/characters.md`. Item stats (damage, armour, weight, reach) are not this file. Code and tests win if they disagree with an older paragraph. Do not invent races, towns, or lore here.

## Current

Clicking the floor sets a `MoveOrder`. Left-clicking a living character (Friendly or Enemy) opens a one-button popup; choosing Attack sets an attack order naming one target id. `Character::set_attack_target` / `attack_target` hold that intent, `attack_velocity_at` closes the distance, and `CombatSession` resolves ids against the characters the scene registers.

`Character::try_melee_swing` resolves one attempt: it pays `MeleeSwing` when it can and marks that swing exhausted when it cannot, runs the defender's block/dodge clock, samples one of six body parts, and applies damage once through `Character::damage`. Attack and defend cooldowns live on each sheet and advance once per `CombatSession::step`.

`in_combat` is derived in C++ from outgoing and incoming attack orders, and `CombatSession::step` regenerates each living sheet with it. The scene no longer passes `false`; only a fixture with no coordinator does. Standing dummies still `set_can_move(false)` and never gain an order of their own.

Skill XP goes through the existing `record_use`; Duelist/Breaker kills and Wall blocks through `record_title_progress`. `OpenCharacter` exposes the target id, `OpenCombat` is the bridge's coordinator handle. `effective_skill`, `target_multiplier`, `held_usable`, `record_use`, and limb weapon gates exist in C++ and tests, and combat reads trained ranks through them rather than effective attributes.

Not built: ranged and magic, squads and multiple-attacker systems, auto-retaliate, bleed, KO vs death, the item catalog, and per-item damage/armour/reach values. Crits (Luck) remain Open.

## 1. Orders — locked

Combat is an order, same idea as walking.

- Floor / ground: move order (already Current).
- A living character: attack order. The ordered character walks toward that target and, once in range, starts the swing loop.

The attack order names one target id. It replaces the current move order. A new move order cancels the attack. Death cancels it (`set_destination` already refuses when dead).

The ordered character keeps that target until you cancel, the target dies, or they can no longer fight (dead, both legs at 0 so they cannot close, both arms at 0 and no unarmed option — Unarmed still needs at least one arm or one leg; that split is Open).

You do not pick each swing. In range, they attack on their own clock.

How the player points: left-click a living character (Friendly or Enemy) opens a tiny popup with Attack. Choosing Attack issues the attack order on that id. Left-click floor is still a move order and cancels the attack. C++ owns the order; Godot only sends the target id.

Auto-retaliate (attack back without an order): Open.

Attacking the same faction is allowed. The popup is how you choose to do it.

## 2. Range — locked

Melee range comes from the weapon. Until the item catalog exists, use one generic constant, separate from `MoveOrder::kArriveRadius` (0.4). Walking uses walk speed and that arrive radius toward a point beside the target, not inside them.

Placeholder range: `1.5` horizontal. Height ignored, same as walking. Out of range → walk. In range → stop walking and swing.

When a weapon has a reach value, that value replaces the placeholder for that swing. Ranged: section 10.

## 3. The swing — locked

One swing is one attempt:

1. Must be alive, in range, and able to use the held weapon (`held_usable`) or Unarmed if nothing usable is held.
2. If current stamina ≥ 8, pay `MeleeSwing` (8). If stamina is below that, still swing: spend nothing extra (cannot go below 0), but this swing’s damage and attack speed are halved (cooldown doubled).
3. Run the defend clock (section 6). Nullify vs full hit is section 5.
4. On a land, apply damage to one body part (sections 5 and 7). Apply the half-damage flag from step 2 here.
5. Start the attack cooldown (section 4). Apply the doubled cooldown from step 2 here.

Two characters can swing at each other. There is no shared turn. Each has their own cooldown.

A swing that does not land is still a swing: stamina is spent if it was paid, cooldown starts.

Half speed / half damage is per swing while stamina is too low to pay 8. It is not a lasting debuff after stamina comes back.

## 4. Attack speed — locked

Fastest in the game: 3 attacks per second (cooldown `1/3` s). Slowest: 1 attack every 5 s. Empty stamina doubles cooldown, then clamp, so the game never leaves `[1/3, 5]`.

The swing uses **trained** rank of the skill for that weapon, not `effective_skill`. Do not divide by effective Agility.

Which skill:
- Empty hands, or no usable held weapon: Unarmed Combat. If Boxing or Taekwondo is chosen, that advanced rank instead.
- A one-handed weapon: One-Handed Weapons, or the matching spec (Katana, Sword, Axe, Mace, Dagger) if that is the weapon and it was chosen.
- A two-handed weapon: Two-Handed Weapons, or the matching spec (Greatsword, GreatAxe, ColosalSword, Warhammer).
- A polearm: Polearms, or Spear / Halberd if that is the weapon and it was chosen.

If the held weapon is under a parent whose spec is a *different* weapon, use the frozen parent basic (100), not the spec. Bows / magic: section 10.

A chosen specialisation unlocks at rank 1 (`characters.md`), so every trained rank a swing reads is `1`..`200`. The `0` row below is the formula's floor, kept for the arithmetic, not a state a sheet can be in.

`t = trained / 200`  
`rate = 0.2 + 2.8 × t` attacks per second  
`cooldown = 1 / rate`  
then clamp to `[1/3, 5]`, then apply empty-stamina double and clamp again.

| trained | rate | cooldown |
|---|---|---|
| 0 | 0.20/s | 5.00 s |
| 1 | 0.214/s | 4.67 s |
| 100 | 1.60/s | 0.625 s |
| 200 | 3.00/s | 0.333 s |

3/s only at advanced 200. Weapon-specific speed inside this band: Open until the item catalog exists, and must still clamp to these caps.

## 5. Damage — locked current slice; typed damage target

The current scalar formula below remains the implemented melee-slice rule. The agreed future weapon system uses cut, pierce and blunt components, individually resisted by armour and summed into one body-part hit; see `weapons.md`. Weapon and shield blocks will mitigate a percentage of damage instead of using the all-or-nothing block test below; a block that prevents any damage grants 1 Blocking XP. A ready dodge automatically mitigates all damage, or 50% when exhausted, and starts the independent defense cooldown using trained Dodging or its chosen matching specialization; exhaustion doubles that cooldown within global limits, and during cooldown the character cannot dodge. The new model is not implemented yet. For its defense choice, a character with a shield equipped uses it; without a shield, a character using a weapon chooses weapon block or dodge by comparing their relevant effective skill values; without a weapon they dodge. Ties between weapon block and dodge favor dodge. A zero-durability shield remains usable at reduced mitigation; if a disabled arm makes it unusable, use the weapon-versus-dodge logic with any usable weapon instead.

Damage must not be `k × effective Strength`. Level multiplies attributes, not HP. Toughness in this formula is the **base**, not effective, for the same reason — and it is the **defender's** base Toughness. The attacker is never asked to soak its own blow.

Same weapon-skill pick as attack speed. Placeholders until the item catalog exists: `weapon = 8`, `armour = 0`, `shield = 0` (no shield). A weapon-block uses that weapon’s block value (placeholder = its `weapon` value). Unarmed dodge has `shield = 0`.

`attack_skill = weapon trained / 200`  
`defend_skill = Blocking or Dodging trained / 200` (section 6)  
`armour` = the piece covering the part that was hit (chest for torso, etc.)

Empty stamina halves `weapon × attack_skill` before the rest.

**Full hit** (block/dodge clock not ready, or ready but the nullify test fails):

`damage = max(0, weapon × attack_skill − toughness_base × armour)`

`toughness_base` is the base Toughness of the character being hit, not the one swinging.

**Nullify** (clock ready, and defender is higher):

`shield + defend_skill > attack_skill × weapon` → `damage = 0`

Equal does not nullify. Dragonslayer ×1.25 applies to `weapon × attack_skill` vs dragons only. Crits (Luck): Open. Magic: section 10.

On damage > 0, call `damage(part, amount)` as in `characters.md`.

## 6. Defense — locked

The defender has a block/dodge cooldown, same idea as attack speed. They do not need a separate defend order.

If the clock is ready when a swing would land, they defend and the clock starts. Nullify vs full hit is the test in section 5, not a separate miss roll. If the clock is not ready, they take a full hit.

Which action and which trained skill:
- A usable shield: block. Blocking, or Small / Big / Wall Shields if that spec matches the shield.
- No shield, two-handed weapon usable: block with the weapon. Blocking (frozen basic if they specced a shield they are not using).
- No shield, one-handed weapon usable: block with the weapon. Blocking.
- Unarmed, or no usable held weapon: dodge. Dodging, or Footwork / Acrobat Evasion if that spec is chosen.

Same rate formula as attack speed, using that defend trained rank, clamped to `[1/3, 5]` s. Shield/weapon-specific block speed inside the band: Open until the catalog.

Limb gates from `characters.md` stand. Both arms at 0: cannot block with a shield or weapon; dodge only if the rules for unarmed dodge still allow it (Open if both arms and both legs are 0).

That Open case cannot be reached, so nothing in `cpp/` gates on it. Body parts are fractions of main HP (`kLimbBodyFraction` is 0.4 per limb) and every part hit also charges main HP, so the four limbs hold 1.6x main HP between them and a sheet dies before its third limb reaches zero. Probed on a level-1 sheet: main HP 100, each limb 40, and the third limb to zero lands with main HP already at 0. If body fractions, main-HP charging, or healing ever change, re-check that claim before writing a rule for it.

## 7. Body part — locked

Each land that still does damage names one part: Head, Torso, Left arm, Right arm, Left leg, Right leg.

Pick uniformly at random among the six (1/6 each). Aimed attacks: Open.

## 8. Combat state — locked

`in_combat` is true while this character has an attack order on a living target, or while a living character has an attack order on them. That flag is what `regenerate(..., in_combat)` already expects. Scene must stop passing `false` once combat exists.

Cancel: new move order, or the target dies. If the target dies, stop swinging; do not auto-pick a new target (squad assist is section 10).

A dead character does not swing, spend, or regen (already locked on the sheet).

## 9. XP from fighting — locked

A successful weapon use is a swing whose damage is greater than 0. That grants 1 XP to the weapon skill used for that swing. Nullify, or any other 0-damage swing, grants 0 to the attacker.

This is **skill XP only**. It does not raise main level. Character level-up is a separate system (Open).

A successful defend is a nullify (clock ready and the section 5 test won). That grants 1 XP to Blocking or Dodging, matching the action they used. A defend that did not nullify grants 0.

Title progress stays as on the sheet: Duelist / Breaker on kills with that weapon class, Wall on successful blocks. Dragons do not exist yet.

## 10. Later — open

Not this file’s first build: bows and crossbows, spell swings, multiple attackers, squads, bleed, KO vs death, auto-retaliate, item catalog, armour soak.

## Ownership

C++ owns the attack order, range test, cooldowns, damage amount, `in_combat`, and skill XP. Godot draws, shows the click popup, and sends the target id. GDScript must not become the source of truth for whether a hit landed.
