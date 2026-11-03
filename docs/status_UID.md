# Status UID

The status interface lets the player read a character's main health, stamina,
mana and level during play. It is presentation, not another character sheet or
simulation. `characters.md` owns the values and their rules; `style.md` owns the
visual direction; `combat_log.md` owns the separate bottom-left action chat.

- **Current** — present in the game today.
- **Locked** — requested and decided; not necessarily built.
- **Proposed** — a suggestion to discuss, not a build order.
- **Open** — not decided; do not implement until agreed.

## Current

The scene has a top-left hint label, character/equipment/technique/workbench
popups, a bottom-left action chat, and floating combat damage numbers. The
**player status HUD is now present**: one scene-owned `StatusOverlay`
(`scripts/status_overlay.gd`) sits on the HUD layer after the chat and before
every popup, refreshes once per drawn frame after the camera follows, and owns
the player plaque — bottom-centre, click-through, showing the sheet name, the
`Level N` line, and three labeled bars (HP/stamina/mana, deep beveled palette,
two-decimal half-up readouts over each pool's own current maximum). Bound to
the actual player sheet; hidden when unbound; never ticks, spends or infers
anything. An unconfigured or missing sheet hides a status rather than showing
a placeholder.

The **world overhead is present in play**: each other character shows its own
identity and pools overhead — sheet name falling back to the scene node name,
the relationship diamond left of it, three unlabeled bars, the `Lv N` plate on
the right, and the authored skull trailing the name when the simulation says
the character is dead (with its real, non-zeroed pools still on the bars). The
legacy floating label is gone. Overheads follow the G2 gate — true 3D
camera-to-feet distance ≤ 50 inclusive, hidden behind the camera — and, per
the human's 2026-11-01 **ship-now-cover-later** decision, **wall coverage does
not hide them yet**; the locked any-part-seen target waits for a ratified
implementation (spike evidence recorded in the plan). The `StatusOverlay` is
the scene's one owner of all of it, refreshing after the camera each frame.

The character sheet and the combat simulation already exist; `OpenCharacter`
exposes the character name, main level and current/maximum health, stamina and
mana to Godot. The current scene sets the player's sheet name to `John Doe`;
sheets may otherwise have empty names — the display rule falls back to the
scene node name. See `characters.md` for the current sheet and pool rules.

The action chat is separate and unchanged: same single event source, same
wording, same bottom-left corner. The player plaque is designed with it in
mind; at small window sizes overlap is possible and accepted (human G5).
Floating damage numbers remain separate from persistent status displays.

## 1. Player HUD — locked direction

A main HUD is fixed at the bottom of the screen for the player's character. It
shows the character's name, current main level, health, stamina and mana: the
basic information the player needs to see while playing. Its layout is locked
below.

The HUD displays the player's actual sheet. It does not decide damage, costs,
regeneration, level progression or other game rules. Main health is the main HP
pool, not the head, torso or limb pools. In particular, a character can die from
head or torso HP reaching zero while main HP is still positive; the main HP
display alone must not be interpreted as a complete alive/dead indicator.

### Player layout — locked direction

A compact bottom-center panel, leaving the existing bottom-left action chat
separate. Name and level form the identity area above three labeled horizontal
bars showing HP, stamina and mana. Each bar shows its current and maximum values
rounded to the nearest hundredth, with two decimal places, for example
`HP 99.96 / 100.00`. A very small positive value can therefore display as `0.00`;
rounding is presentation only and does not alter the actual pool.

**Locked 2026-11-01 (human).** Rounding ties go **half-up** (`99.995 → 100.00`),
through one shared formatter so the three readouts can never disagree. The plaque
is a **solid opaque wood panel** (`#241B16`) with a **double brass frame** (1st
`#806343`, cast-shadow band `#2E231C`), approximately **340×90 px at 1280×720**,
**bottom-center, 16 px above the bottom edge**; each bar is **10 px** tall with a
**62 px label column** and **100 px right-aligned value column**. Name and level
share one identity row: name left, `Level N` right. Long names follow the shared
20-character truncation rule (section 3). At small window sizes the plaque stays
bottom-center and **may overlap the action chat — accepted**, no reflow rule.

**Pointer input — locked.** Clicks on the player status HUD pass through to the
game world, like the existing action chat.

```text
                 [ Character name    Level N ]
                 [ HP       ========  99.96 / 100.00 ]
                 [ Stamina  ========  85.00 / 100.00 ]
                 [ Mana     ========  85.00 / 100.00 ]
```

This is a layout sketch, not approved dimensions, colors, numbers or a promise
that the panel fits every viewport without adaptation.

## 2. World characters — locked direction

Other world characters display three bars above their head: one each for main
HP, stamina and mana. A level gauge sits on the side of those bars. These are
views of each character's own sheet, not the player's pools. The character's
name appears above the bars.

**Visibility — locked direction.** Overhead status is visible for both allies
and enemies during normal play; it does not require hovering over the character
or being in combat. Hide the overhead status when the character is completely
behind a wall or other solid obstacle; keep it visible under partial cover as long
as any part of the character can be seen. The maximum viewing distance is **50
Godot units**, measured from the camera to the other character. This value is
locked for now and can be tuned after playtesting. A character's overhead status
remains after death and shows a skull icon alongside the relationship diamond to
indicate that the character is dead. The skull reflects the character's actual
death state, not an inference from main HP: head or torso HP can reach zero while
main HP remains positive.

**Locked 2026-11-01 (human) — visibility boundaries.** Distance is the true 3D
Euclidean line from the camera to the actor's **feet origin** (its node
position, unaffected by animation); a character is visible while that distance
is **≤ 50.0 — exactly 50 counts**. A character **behind the camera is hidden**.
There is **no screen-edge rule**: the control simply leaves the frame with its
projected anchor. Overlapping overheads from crowded characters **overlap
freely** — no nudging, no priority culling. The status's lifetime **equals the
actor node's lifetime**: it exists while the actor node exists, survives death
(a standing corpse keeps its overhead with real pool fills), and disappears
when the actor is freed; there is no fade and no separate despawn behavior, and
a future corpse-removal mechanic will take the overhead with it for free.

**Locked 2026-11-01 (human) — skull.** The death icon is a **project-authored
vector skull** at `assets/ui/status_skull.svg` (single-color silhouette; no
external asset). It trails the name line — `◇ Name 💀` — drawn in the
light-parchment text color `#F4E6C6` at approximately the name text's cap
height. The diamond always remains; the skull is driven **only** by the
simulation's death state.

**Engineering gate — decided 2026-11-01 (human).** The "any part seen / hide
only when completely covered" rule remains the locked target. The spike
process ran: an actor-ID raster pass was demonstrated end to end (sliver,
head-only, full cover, turned body, actor-behind-actor all classified
correctly; evidence in the plan), but its production cost needs a C++/RD
implementation. The human then decided **ship now, cover later**: overheads
are live with range and facing gating only, walls do not hide them, and the
exact cover pass ships as a later change with its own authorization. Until
then, "hide only when completely covered" is the design's intent, explicitly
unimplemented — not permission for any approximation to pass as it.

**Relationship indicator — locked direction.** A small colored diamond beside
the character's name, separate from the HP and stamina bars, is green for an
ally and red for an enemy.

**Locked 2026-11-01 (human).** The diamond sits **left of the name**
(`◇ Name`), its fill **clones the resource-bar shades**: ally green `#4E7A4A`,
enemy red `#8F423D`. Relationship stays strictly **two-state** (friendly /
enemy) — no neutral or other presentation is defined; presenting any possible
future relationship remains Open.

### World layout — locked direction

Stack the three horizontal bars above the character, with the level badge to the
right of the stack. The bars show their fill without numerical values or text
labels; their fixed order and colors distinguish the resources. Clicks on the
overhead status display pass through to the game world.

**Locked 2026-11-01 (human) — overhead layout.** The name line (`◇ Name`, plus
trailing skull when dead) sits centered above three stacked bars, each
approximately **120×6 px** with **13 px name text**, the level badge to the
right of the stack, vertically centered. The overlay's **bottom edge anchors
about 2.3 units above the actor's feet** — the legacy label's clearance — and
follows the actor. The level badge is a **brass-framed wooden plate** (fill
`#493326`, border `#806343`, `Lv N` in light parchment `#F4E6C6`). Exact sizes
are verified once against the production models on the desktop pass.

```text
                    ◇ Character name
                    [ ============ ]  [ Lv 3 ]
                    [ ============ ]
                    [ ============ ]
                         character
```

**Level badge — locked.** A small medieval-style badge displays the character's
current main level as a number, for example `Lv 3`. It shows the level itself,
not experience or progress toward the next level. Level is currently an integer
from 1 through 7; earning levels is not built or defined by this interface.

## 3. Shared presentation — mixed

**Bar colors — locked.** Use muted red for HP, green for stamina and blue for
mana, consistently on the player HUD and world-character bars.

**Locked 2026-11-01 (human) — shades and treatment.** Deep, beveled fills over
sunken tracks: HP `#8F423D`, stamina `#4E7A4A`, mana `#4A6690`; track
`#2E231C`; bar frame `#6E5539`; a soft top highlight on the fill. An empty bar
shows the bare track; a full bar is entirely filled color. Relationship
diamonds clone the green/red. Text follows `style.md` parchment tones.

**Bar order — locked.** Both displays stack the bars top to bottom as HP,
stamina, then mana.

**Bar fill — locked.** Each bar represents its own pool's current value
relative to its current maximum. For example, `50 / 100` fills half the bar.
This applies to both the player HUD and world-character bars; bars belonging to
different pools or characters can have different maxima.

**Locked 2026-11-01 (human) — degenerate data.** If a maximum is zero, the bar
draws **empty** (fill 0, no crash) and the player text prints the true numbers
anyway (e.g. `0.00 / 0.00`). If a status's sheet is missing, freed or invalid,
that status **hides entirely** and reappears when the data is valid again —
never a placeholder or half-truth.

**Bar updates — locked.** Both displays update their bar fill immediately when a
pool value changes, without a delayed drain or refill animation.

**Locked 2026-11-01 (human) — update mechanism.** Every status re-reads
current **and** maximum from the bridge **once per drawn frame, after the
camera has followed** — maxima are never cached, so a mid-play maximum change
shows the same frame it happens. Statuses expose an explicit `refresh()` seam
for deterministic tests; normal play drives the same path automatically.

**Direction adopted 2026-11-01 (human):**

- `style.md`'s rustic/medieval direction is adopted with the concrete palette
  locked above; status surfaces use it without popup title chrome — except the
  player plaque, which the human explicitly chose as a solid framed panel.
- Presentation stays readable against the world; the action chat, popups and
  floating damage feedback keep their places. The player HUD never moves out of
  the bottom center; **overlap with the chat at small window sizes is accepted**
  rather than resolved by reflow. Crowded overheads may overlap.
- Statuses draw from the live character state exposed through the bridge only —
  Godot owns the display, never a second copy of authoritative gameplay state.

Character naming for the display layer is likewise **locked 2026-11-01 (human)**
in both views: show the character's **sheet name**, falling back to the actor's
**scene node name** when the sheet name is empty; names longer than 20
characters display as the first 19 plus `…`. The action chat keeps its existing
scene-node wording regardless.

## 4. Decision walk

1. **Player HUD layout — Locked:** bottom center, name and level above three
   labeled horizontal bars, separate from the bottom-left action chat. Each bar
   shows its current / maximum value, rounded to the nearest hundredth, two
   decimals, **ties half-up**. Solid wood plaque, double brass frame, fixed
   pixel set (see section 1); small-window overlap with the chat accepted.
2. **World layout — Locked:** name above three stacked bars with the level
   badge to the right; clicks pass through. Overhead sizes, badge plate and
   the ~2.3-unit anchor are locked in section 2.
3. **Level badge — Locked:** small medieval-style badge showing the current
   main level as a number, not experience or progress toward the next level.
4. **Resource presentation — Locked:** values two-decimal half-up (player only),
   unlabeled world bars, deep beveled red/green/blue in HP / stamina / mana
   order, each pool against its own current maximum, immediate fills. Empty =
   bare track; zero maximum draws empty with truthful text; maxima re-read live.
5. **World visibility — Locked except the cover algorithm:** normal-play
   visibility both factions; hide only under complete solid cover, visible if
   any part can be seen — the exact realizable contract for that rule is under
   a ratified-process engineering gate (spike → evidence → human decision), and
   the world overlay ships only after it. Distance: camera → feet origin, true
   3D, ≤ 50 inclusive, hidden behind camera, no screen-edge rule. Skull
   authored in-repo, trails the name, driven by true death only. Diamond left
   of name, shades clone the bars; two-state relationship. Empty names fall
   back to the scene node name; names truncate at 20 to 19+`…`. Crowds overlap
   freely.
6. **Player HUD behavior — Locked:** clicks pass through. Identity rules as
   above; scaling rides the project's canvas stretch; plaque stays
   bottom-center always.
7. **Data and lifecycle — Locked:** refresh once per drawn frame after the
   camera; current and maximum re-read every frame; invalid or missing sheet
   hides that status; status lifetime equals actor-node lifetime; corpses keep
   overheads until their actor goes; no fades, no invented spawn systems.

Remaining Open: the neutral/other-relationship presentation (nothing produces
it), the exact cover algorithm pending ratification, and anything a future
system introduces. No implementation is authorized by a Proposed or Open
entry.
