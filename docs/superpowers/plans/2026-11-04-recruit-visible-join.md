# Recruit visible join implementation plan

> For later OpenCode jobs: implement only the single Task supplied by the
> manager, in sequence. Read Interfaces and Global Constraints first. Stop after
> that Task's verification. Do not start the next Task. Do not commit. Do not
> read `.superpowers/` or unrelated style essays. This document is a plan, not a
> roster, not a dismiss button, and not authorization to make anything on the
> squad plaque clickable.

**Goal:** When an ask gets an answer, the player sees it three ways and one way
only per view: the right-edge squad plaque prints who is seated, the action chat
says the answer once in its own voice, and a small card over the head that
answered says `YES` or `NO`. A squad starts founded on the character the scene
commands. An ask that breaks — walked away, a fight chosen, its target dead
first, a blow landing on the recruiter — is said by nobody: no line, no card.

**Architecture:** `open::Character` keeps the answer as a stamp (whose ask it
answered, and what it said), and `CombatSession` founds a squad by seating a
sheet. That is the whole new simulation surface. The bridge carries the seat as
one write door and the stamp as one read door, because a half an answer is not an
answer. Godot adds two views — `SquadList` and `RecruitBubble` — plus one chat
sentence pair, all of them pure views of what C++ already settled: no polling
that infers a happening from a number, no event queue, no drainer.

**Tech stack:** C++20 `open_sim`, existing `test_support.hpp`, CMake/CTest,
Godot 4.7.2, GDExtension, GDScript headless tests.

**Spec:** `docs/recruiting.md` — Founder seated, The answer is stamped, Who tells
the player, The squad plaque, The answer card. `docs/combat_log.md` for the two
recruit sentences. `docs/status_UID.md` for the naming rule and the update
mechanism the card borrows. Mockups the design was played back on live under
`.superpowers/brainstorm/785709-1793793727/content/`.

## Live facts (inspect these, do not reinvent)

- The recruit order already walks, attempts, cancels and provokes
  (`2026-11-03-recruit-joining.md`). Only the *leaving* of an order was visible
  before this slice, and only as a flag on a sheet.
- `CombatSession` clears the recruiter's order and stamps the sheet it asked in
  the **same** `step`. So the question an issuer can ask after a step is only
  "is that order still held?"
- `Character::recruit_target()` is the recruiter's; the answer stamp lives on the
  **target**. A test that corrupts one and reads the other proves nothing.
- `Character::in_squad_` is written in exactly three places: death, a successful
  answer, a seat. There is no `set_in_squad` and the bridge's only membership
  door is the read `is_in_squad`.
- Bridge naming: `OpenCombat::found_squad(actor_id)`,
  `OpenCharacter::get_recruit_target_id()`, `OpenCharacter::get_recruit_answer()`.
- The HUD canvas layer is built in `main._build_hud`, in draw order: hint, chat,
  status overlay, squad plaque, answer bubble, then every popup.
- The status overlay owns the projection (`head_anchor`) and the view gate; the
  overhead plate's height is its own constant (`world_status.PLATE_HEIGHT`).
- Godot 4 processes the **lower** `process_priority` first. The world's camera
  follow ticks at 0, so "after the camera" is a positive priority.

## The decisions this slice was built to

- **The issuer reports (C / C-2).** The node that ordered the ask is the node
  that reports its answer, and it reports only an answer stamped to *its own*
  asker id. A stamp naming somebody else is another recruiter's news. Today the
  player is the only character anybody commands, so this is one watcher in
  `main.gd`, armed when `recruit_requested` fires.
- **The stamp is two halves that arrive together (the agent's call, seen).**
  `answered_to` without `answered_accepted` cannot tell a join from a refusal,
  and `answered_accepted` alone says *no* for a sheet nobody asked. So the bridge
  carries one read, `get_recruit_answer`, that answers `{asker_id, accepted}` or
  `{}`. A lone-id door was built first and retired for exactly this reason.
- **Founding seats, and seats nothing else (human).** `found_squad` writes
  membership only: faction, recruitable flag, condition, orders and the answer
  stamp stay untouched; a corpse is refused; founding twice seats one seat.
- **The plaque is told its founder (human).** It is presentation; C++ keeps no
  "who founded" query, so ordering is founder-first-then-id by what the scene
  handed it, not by a fact the simulation pretends to remember.
- **A card over the player must still be possible (the agent's call, seen).** The
  player owns no overhead plate, so the card asks the gate's *camera* half
  (`camera_shows`) rather than its plate half (`status_visible`).

## Technical defaults (not new gameplay)

- Stamp fields: `std::optional<std::uint64_t> answered_to_`,
  `std::optional<bool> answered_accepted_`; written only by
  `apply_recruit_success` / `apply_recruit_failure`. Death keeps them.
- The plaque is always on: the `SQUAD` heading stands over zero rows and the
  panel never hides itself. Right edge 16 px in, vertically centred, 176 px
  wide, cast band `#2E231C` / brass `#806343` / wood `#241B16`, heading
  `#BD9862`, names `#F4E6C6` in the serif stack.
- The card is `46×24` plus an 8 px tail, badge wood `#493326`, brass
  `#806343`, one parchment word `#F4E6C6` for **both** answers. It holds 1.0 s,
  fades over 0.6 s, and does not move except with the head it points at.
- Both views are `MOUSE_FILTER_IGNORE`, have no button on them, and are re-read
  every drawn frame (`_process`), never refreshed by a signal from a guess.
- Names come from `status_presenter.gd::display_name`, which is now the one rule
  the whole HUD reads — the chat's older scene-node wording is retired.
- Timings, the empty-squad panel, the card's parchment-only verdict and the
  issuer's silence on a broken ask are the agent's calls the human saw, not
  separate asks.

## Global constraints

- C++ owns membership, the stamp and the founder seat. Godot presents and sends
  ids. No GDScript may infer that somebody joined because a number moved.
- No event queue and no drainer for recruiting. There is already one drainer for
  swings (`CombatFeedback`) and the chat has two feeds into one eight-entry
  history; that is not a template to copy.
- No dismiss, no re-recruiting, nothing clickable, no player-death display. Death
  is the only way a member leaves.
- No Godot types in `cpp/`.
- No roster, no switching who you command, no item/title conditions, no cost, no
  lore, no new camera.
- Every Task: failing tests first, observe red, implement only that Task, then
  that Task's tests and full `ctest`. Godot Tasks rebuild `libopen.so` and
  re-import before running scripts.
- Before any Godot script after C++/extension changes:
  `cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON`,
  `cmake --build cpp/build -j"$(nproc)"`,
  `ctest --test-dir cpp/build --output-on-failure`,
  `godot --path . --headless --import --quit`.
- No commit. No `cpp/build/`, `extension/bin/`, `.godot/`, `*.so`.

## Out of scope

| Thing | Why |
|---|---|
| Roster, click-name-to-control | Locked out until asked |
| Dismiss / desert / kick | Locked: death only |
| Re-asking a sheet who said no | Locked: the flag is spent |
| Item / title recruit conditions | Open in `recruiting.md` |
| A second recruiter besides the player | Nothing commands anybody else |
| A view of who founded the squad | C++ keeps no such fact; the plaque is told |

## Coverage matrix

| Behavior | Live before | This plan |
|---|---|---|
| Squad at scene start | empty membership | founded on the player, seated |
| An ask is answered | a flag flips on a sheet | chat line + card + plaque row |
| An answer's owner | not recorded | stamped with the asker, read as one dictionary |
| A foreign recruiter's answer | n/a (none exist) | stays unspoken here |
| A broken ask | silent, invisibly | still silent: no line, no card |
| A member dies | `in_squad` false | the row is gone next frame |
| Squad with nobody seated | n/a | heading over zero rows, panel still on |
| Card over the player | n/a | shows: the gate is the camera's |
| Chat naming | scene node names | the presenter's rule, like every other view |

## Review focus

- The stamp must be unreadable as a half-answer (`{}` vs `{asker_id, accepted}`).
- A seat must touch no order, no flag and no stamp (mutation-tested).
- A blow on the recruiter must leave the asked sheet **unstamped** (both
  halves), and no display may notice the cancelled order (mutation-tested).
- The plaque must not be a second membership: it re-reads `is_in_squad`.
- The card must not be reachable from a sheet: it has no door back in.
- "After the camera" is a positive `process_priority`. A negative one is earlier
  than the world and projects last frame's camera.

---

### Task 1: The seat and the stamp in C++

**Files:**
- Modify: `cpp/include/open/character.hpp`, `cpp/src/character.cpp`
  (`apply_found_seat`, `answered_to`, `answered_accepted`)
- Modify: `cpp/include/open/combat_session.hpp`, `cpp/src/combat_session.cpp`
  (`found_squad`, stamp on answer)
- Modify: `cpp/tests/recruit_squad_test.cpp`, `cpp/tests/recruit_order_test.cpp`

**Interfaces:** `bool Character::apply_found_seat()` — refuses a corpse, writes
`in_squad_` and nothing else. `bool CombatSession::found_squad(actors, id)` —
absent or zero refused, idempotent. An answered attempt sets both halves.

- [x] **RED:** founding a corpse seats nobody; founding twice seats one; a seat
  leaves faction, flag, condition, orders and the stamp exactly as they were; an
  answer names the asker.
- [x] Observe red (no such doors).
- [x] Implement the seat and the stamp only.
- [x] GREEN: full `ctest`.

### Task 2: Carry it across the boundary

**Files:**
- Modify: `extension/combat_bridge.cpp` (`found_squad`),
  `extension/character_bridge.cpp` (`get_recruit_answer`)
- Modify: `scripts/tests/recruit_bridge_test.gd`

**Interfaces:** `found_squad(actor_id) -> bool`;
`get_recruit_answer() -> Dictionary` — `{}` when nobody was answered, else
`{asker_id, accepted}`. A lone-id door was tried, found unable to tell a join
from a refusal, and retired inside this slice.

- [x] **RED:** an unasked sheet answers `{}` — not `false`, not `0`; after a yes
  the dictionary names the recruiter and says true; after a no it says false.
- [x] Observe red (no door).
- [x] Implement, rebuild `libopen.so`, re-import.
- [x] GREEN: `recruit_bridge_test.gd`, plus the bridge suites that name characters.

### Task 3: The chat says the answer, in the HUD's one voice

**Files:**
- Modify: `scripts/combat_log.gd` (`append_recruit`, `_recruit_sentence`,
  `_name_of` through `status_presenter.gd`)
- Modify: `scripts/tests/combat_log_test.gd`

**Interfaces:** `append_recruit(event)` where `event` carries `actor_id`,
`target_id`, `accepted`. Two sentences, `X recruited Y` and
`X failed to recruit Y`, into the **same** eight-entry history as swings.

- [x] **RED:** the two sentences, unknown ids as `Character N`, and ten writes
  leaving eight entries whatever mix of kinds wrote them.
- [x] Observe red.
- [x] Implement; retire the chat's scene-node naming.
- [x] GREEN: `combat_log_test.gd`, `status_integration_test.gd`.

### Task 4: The founder and the plaque

**Files:**
- Create: `scripts/squad_list.gd`
- Modify: `scripts/main.gd` (`found_squad` at setup, `_build_hud`, per-frame
  `refresh`)
- Create: `scripts/tests/squad_plaque_test.gd`

**Interfaces:** `bind_squad(actors, founder)`, `refresh()`. Rows are a view of
each sheet's own `is_in_squad`; order is founder first then character id; the
panel is always visible; nothing on it takes input; it draws under the popups.

- [x] **RED:** the founder is seated at scene start and is the plaque's first
  row; an empty squad still shows the heading; joining adds a row and death
  removes one, unprompted; the locked placement, palette, serif and click-through.
- [x] Observe red (no such node).
- [x] Implement the node and the founding call.
- [x] GREEN: `squad_plaque_test.gd`, `status_integration_test.gd`.

### Task 5: The card and the issuer

**Files:**
- Create: `scripts/recruit_bubble.gd`
- Modify: `scripts/main.gd` (`_ask` watcher, `_report_recruit_answer`),
  `scripts/status_overlay.gd` (`head_anchor`, gate), `scripts/world_status.gd`
- Create: `scripts/tests/recruit_answer_test.gd`

**Interfaces:** `bind_view(overlay)`, `open_for(actor, accepted)`, `word()`,
`tick(delta)` — public so a test can knock without a signal. The watcher arms on
`recruit_requested`, and after each `_combat.step` asks whether that order is
still held; when it is gone it reads the stamp and reports only if the stamp
names its own asker.

- [x] **RED:** a yes reports once in the chat and says YES over her; a no says
  NO; a foreign ask is its own business; the four broken asks are silent; hold,
  fade, then nothing; the card stacks over the plate and obeys the gate.
- [x] Observe red.
- [x] Implement the card and the watcher.
- [x] GREEN: `recruit_answer_test.gd`, `recruit_joining_test.gd`,
  `melee_combat_test.gd`.

### Task 6: The docs and the mockups

- [x] `docs/recruiting.md`: founder, stamp, who tells the player, the plaque, the
  card, the silence rule.
- [x] `docs/combat_log.md`: the two recruit sentences and the naming lock.
- [x] `docs/status_UID.md`: the naming rule covers the chat.
- [x] The design mockups the human played back on, kept in the repo.

## Verification (after all Tasks)

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure          # recruit_sheet/order/squad
godot --path . --headless --import --quit
godot --path . --headless --script scripts/tests/squad_plaque_test.gd
godot --path . --headless --script scripts/tests/recruit_answer_test.gd
godot --path . --headless --script scripts/tests/recruit_bridge_test.gd
godot --path . --headless --script scripts/tests/recruit_joining_test.gd
godot --path . --headless --script scripts/tests/combat_log_test.gd
godot --path . --headless --script scripts/tests/status_integration_test.gd
```

Desktop visual: recruit Jane and watch the row, the line and the card; ask from
across the floor and walk away — nothing is said. Not done in headless.

## What this does not ship

The roster and switching who you command; a second recruiter; any way to dismiss
a member; item or title recruit conditions; a cost; a plaque that can be clicked
into anything.

## Result and review follow-ups

Shipped as `a0ce7f2`…`79c694a`, with the design mockups at `ba516e0`. A
docs-vs-code review of the shipped slice then found three things, all fixed:

- **A — the status views were ticking last frame's camera.** The comment in
  `status_overlay.gd` claimed a negative `process_priority` ran after the world;
  Godot 4 runs the lower value first, so the overlay was 25–33 px off its own
  anchor during a camera move. Fixed at 50 (card 60), and now pinned by
  *measurement* in `status_integration_test.gd` rather than by a sign.
- **B — the card borrowed the plate half of the gate**, which a player who
  answered her own squad can never satisfy because she owns no overhead plate.
  The overlay now publishes the camera half (`camera_shows`) and the card uses
  that; the plate's height is `world_status.PLATE_HEIGHT`, said once.
- **C — three paragraphs of docs were stale or over-committed**: the chat's
  retired naming rule, the HUD inventory without the two recruiting views, the
  seat's "no door writes membership" (death does write it), the stamp's "neither
  does a later ask" (a later *answered* ask owns it), and the plaque's cast band.
  `scripts/combat_log.gd`'s header now says the chat has two feeds into one
  history.

The pins that came out of the review: `Membership` in `recruit_order_test.cpp`
carries the stamp, three new blocks in `recruit_squad_test.cpp` (a seat touches
no order, a blow leaves the asked sheet unstamped, the latest answered ask owns
the stamp), the palette and draw-order pins for the plaque and the card, the
shared-history pin in `combat_log_test.gd`, and a display-side silence case for
a blow on the recruiter. Each of the C++ pins was checked by mutating the line
it guards and watching exactly that assertion fail.
