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

The squad starts founded: the scene asks the coordinator to seat the player as
its founder before anybody has asked anybody anything. Jane Doe 1 starts
recruitable and her condition is the dummy that always succeeds; the enemy dummy
and the player do not carry the flag. Left-click a living character opens the
popup with Attack, and with Recruit beside it when that character's recruitable
flag is set - `attack_popup.gd` shows the second button and issues nothing
itself. Choosing Recruit issues a recruit order on the currently commanded
character: C++ walks them to the unarmed melee 1.5 (a held weapon's reach does
not replace it), and the attempt runs on a later frame, in range, never on the
click. A yes puts the target in the squad and sets faction Friendly; a no spends
the flag and moves nothing else, and either answer stands the recruiter down.
The answered sheet keeps the stamp of whose ask it answered and what it said,
which is how the scene that issued the ask tells its own answer from somebody
else's. A new move order, a chosen attack, the target's death, or a swing
performed against the recruiter — dodged, blocked or landing, any performed swing
counts, the same reading `combat.md` makes of a provocation — all end the ask
without an answer: no flag spent, nobody joined, and nothing said. Death is the
only way a member leaves.

The join is visible: the squad plaque on the right edge prints who is seated,
the bottom-left chat says the answer once in its own voice, and a yes-or-no card
hangs over the head that answered for as long as it takes to read. All three are
views — they read membership and the stamp, they never write anybody's state, and
a broken ask shows nothing at all.

Still nobody to recruit a second time - the flag is spent by the first answer,
and there is no way back. Only the founder is in a squad at the start, Jane stays
unable to walk after joining (`set_can_move(false)` is hers still), and there is
no roster, no switching who you command, no item or title condition language,
and no cost. Faction is `Friendly` / `Enemy` only; neutral does not exist.
`characters.md` already says player and NPC are the same type, and the player is
the character you currently issue orders to — that is selection after joining,
not how someone joins.

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

## Founder seated — locked

**Locked 2026-11-04 (human).** A squad starts with somebody in it. The coordinator
seats the character the scene commands — the player — as its founder before
anybody has been asked for anything, through `CombatSession::found_squad`.
A seat writes membership and nothing else: the faction a sheet was configured
with, its recruitable flag and its own recruit condition stay exactly as they
were, and no answer is stamped, because nobody was asked. A corpse cannot be
seated (death is the only way a member leaves, and a seat cannot bring anybody
back), an absent or zero id is refused, and founding twice seats the one seat it
already seated. Nothing else seats anybody: there is no public `set_in_squad`, and
no door that writes membership from a script.

## The answer is stamped — locked

**Locked 2026-11-04 (human).** An ask that gets an answer leaves a trace on the
sheet that was asked, and the trace is the whole answer: *whose* ask it answered
(`answered_to`) and *what it said* (`answered_accepted`). Both halves arrive
together or not at all, because only an attempt stamps them. A seat is not an
answer, and a cancel, a death or a swing is not an answer either — an ask that
broke stamps nothing. Death does not take an answer away, and neither does a
later ask: the stamp belongs to the recruiter who got it.

**Two halves, not a half-answer (the agent's call, seen 2026-11-04).** The bridge
carries both halves as one read (`get_recruit_answer`): an empty Dictionary means
nobody was answered at all, never "answered no". A lone id could not tell a join
from a refusal, and a lone bool could say *no* for a sheet nobody asked — the
display would then write a sentence about an answer that was never given.

## Who tells the player — locked

**Locked 2026-11-04 (human).** The character that was ordered to ask is the one
whose answer gets told, and only that issuer reports it. The scene commands the
player today, so a stamp naming anybody else is another recruiter's news and
stays unspoken here. There is no event queue for recruiting and no drainer:
the issuer compares the stamp against its own ask.

**Silence is the rule, not an option (human, 2026-11-04).** A broken ask — walked
away, a fight chosen instead, the target dead first, a swing landing on the
recruiter — produces no chat line and no card. Nothing on screen may infer a
happening from a snapshot either: the plaque, the chat and the card read what C++
says; they never decide that somebody joined because a number moved.

## The squad plaque — locked

**Locked 2026-11-04 (human).** Membership is printed on the **right screen edge**,
16 px in and vertically centred, as a wood plaque (`#241B16`) in a brass frame
(`#806343`) under a `SQUAD` heading (`#BD9862`) and the names below it
(`#F4E6C6`, serif), through the same name rule every other view uses.

It is **always on**: the heading stands over zero rows, because an empty squad is
still the squad, and the panel never hides itself because membership went quiet.
The rows are a pure view — every sheet's own `is_in_squad` answer, re-read on the
drawn frame, so a member who joins or dies is on or gone on the next frame with
nobody asked to refresh anything. The order is the founder first, then the rest by
character id; the plaque is *told* who the founder is, because it is presentation
and C++ keeps no "who founded" query.

Nothing on it is clickable, there is no dismiss and no re-recruiting, and it
ignores the mouse: a click beside the names is a click on the world. It draws
under the popups, the same as the other HUD views.

## The answer card — locked

**Locked 2026-11-04 (human).** The answer the issuer reports is also said over the
head that said it: a small card with a tail pointing at the character who
answered, wearing the badge wood `#493326` and brass `#806343` of the level plate,
and one word — `YES` or `NO`. Both words are parchment ink `#F4E6C6`: a no is an
answer, not an error. The parchment-only verdict is the agent's call, seen
2026-11-04.

The card **holds about a second, then fades in most of a second, and does not
move** besides following the head it points at — it borrows the status overlay's
anchor and gate instead of writing a second rule for where a head is or what the
camera can show, so an out-of-range or behind-the-camera answer hides rather than
being painted at a guessed place, and comes back on the word it was already
saying. Those timings are the agent's call, seen 2026-11-04. It stacks over the
answered character's own overhead plate rather than covering it. Nothing on it can
be dismissed, clicked or asked again.

## Open

Item and title condition schemas. The Jane always-succeed join order is locked
enough to implement. Roster and switching who you command wait until you ask
for that slice.
