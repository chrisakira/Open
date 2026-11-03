# Recruiting

This file owns how a character joins the player's squad. The roster, selecting a
recruited character, and issuing orders to them wait until joining is locked.

- **Current** — present in the game today.
- **Locked** — requested and decided; not necessarily built.
- **Proposed** — a suggestion to discuss, not a build order.
- **Open** — not decided; do not implement until agreed.

Code and tests win if they disagree with an older paragraph. Do not invent towns,
dialogue trees, or lore here.

## Current

Nobody is in a squad. The scene has the player (`John Doe`, friendly), a standing
friendly dummy (`Jane Doe 1`), and a standing enemy dummy (`John Doe 1`). Both
dummies call `set_can_move(false)`. Left-click a living character opens the Attack
popup; there is no recruit action. Faction is `Friendly` / `Enemy` only; neutral
does not exist. `characters.md` already says player and NPC are the same type, and
the player is the character you currently issue orders to — that is selection after
joining, not how someone joins.

## Slice order — locked

**Locked 2026-11-03 (human).** Design recruiting first (how someone joins), then
the roster. Jane does not start already recruited as a placeholder for this slice.

## Who can be asked — locked

**Locked 2026-11-03 (human).** Eligibility is a separate **recruitable** flag on
the sheet, independent of faction. Friendly does not imply recruitable. Enemy
does not forbid it. Neutral is still absent; this flag is not a third faction.

The flag is character state (`cpp/`), not a node property.

## Starting flag — locked

**Locked 2026-11-03 (human).** In the current scene Jane Doe 1 starts
recruitable. The enemy dummy does not. The player is the asker, not a recruit
target. This is the scene's starting values, not a rule that enemies can never
have the flag.

## How the player asks — locked

**Locked 2026-11-03 (human).** Left-click a living character still opens the
existing popup. Attack stays on it. When that character's recruitable flag is
set, the same popup also offers Recruit. A character without the flag is
Attack-only, same as today.

`combat.md` still owns Attack. This file owns Recruit on that popup. Opening or
dismissing the popup does not recruit.

## Choosing Recruit — locked

**Locked 2026-11-03 (human).** Choosing Recruit is an attempt that can fail.
Success is not guaranteed.

## When the attempt resolves — locked

**Locked 2026-11-03 (human).** Choosing Recruit issues a recruit order: the
character you currently command walks into range of the target, then the attempt
runs and can still fail. It is not resolved on the click, and it is not refused
just because you started out of range.

C++ owns the order; Godot only sends the target id.

## Hit while recruiting — locked

**Locked 2026-11-03 (human).** A performed swing at a character that holds a
recruit order — walking or already in range for the attempt — cancels that
order and does **not** auto-retaliate. They stop: no recruit order, no attack
order. Not a failed attempt: faction and recruitable stay as they were. The
swing is still a provocation in every other sense `combat.md` already names;
this only blocks taking the attacker as an attack order.

## New move while recruiting — locked

**Locked 2026-11-03 (human).** A new move order cancels the recruit order and
the character walks there, same as a floor click cancelling an attack. The
attempt does not run. This is not a failed attempt: faction and recruitable
stay as they were.

## Target dies while recruiting — locked

**Locked 2026-11-03 (human).** If the recruit target dies while the recruit
order is active, the order ends. No attempt runs. Flag and faction stay as they
were. This is not a failed attempt.

## Attack while recruiting — locked

**Locked 2026-11-03 (human).** Choosing Attack on anyone cancels the recruit
order and issues that attack. The recruit attempt does not run. This is not a
failed attempt: faction and recruitable stay as they were.

## Recruit range — locked

**Locked 2026-11-03 (human).** In range is the unarmed melee fallback **1.5**,
horizontal, Y ignored — the same measurement as walking and bare-hand melee.
Held-weapon reach does not replace it. Walk arrive radius `0.4` is not the
recruit range.

## The check — locked

**Locked 2026-11-03 (human).** Each character has their own recruit conditions.
Those can be a skill check, a required item, or a required title. This is not
one global formula.

**Jane Doe 1, for now (revisitable).** Her condition is a dummy that always
succeeds. That is the playable placeholder so joining can run; it is not the
final condition language and not a promise that every recruit succeeds.

No other character's conditions are specified. Item and title predicates have
no schema yet.

## Cost — locked for now

**Locked 2026-11-03 (human).** No cost on a Recruit attempt for now. Revisitable.
The attempt does not spend stamina, mana, items, or money.

## Success — locked

**Locked 2026-11-03 (human).** A successful attempt puts the target in the
player's squad and sets their faction to **Friendly**. An already-friendly
target still joins; they are Friendly either way.

This is membership plus faction, not the check.

## Flag after success — locked

**Locked 2026-11-03 (human).** Success clears the target's recruitable flag.
They are in the squad; their popup is Attack-only again.

## Failure — locked

**Locked 2026-11-03 (human).** A failed attempt does not join and does not
change faction. It clears the target's recruitable flag: one attempt, then
Attack-only. This is not a lock that they attack you, and it is not a way to
set the flag again.

## Leaving — locked

**Locked 2026-11-03 (human).** Once in the squad, they stay until they die.
Death removes them. There is no dismiss, desert, or other leave. This does not
decide faction or the recruitable flag on a corpse.

## Open

Item and title condition schemas. The Jane always-succeed join order is locked
enough to implement. Roster and switching who you command wait until you ask
for that slice.
