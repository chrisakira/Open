# Combat log

Presentation overlay. It does not own simulation. C++ decides what happened; Godot only formats and draws.

- **Current** — true in the scene today.
- **Locked** — decided for this file. Built only where the same text is also marked Current.
- **Proposed** — written so we can lock it. Not a build order.
- **Open** — do not implement until the human locks it.

## Current

The scene HUD is a centred dark-and-brass control hint plus the character, equipment, technique and workbench popups, and the bottom-left action chat below them. The chat fits to the left of the player plaque at the 1280×720 and 720×420 logical viewport sizes. At narrow widths it can grow taller to fit the newest report, up to 60% of viewport height; unusually long reports can still be clipped, and older reports remain in the eight-entry history. Canvas stretch scales text down in a smaller physical window. Combat presentation is 3D: held meshes, skeletal attack clips, floating damage numbers.

**The chat exists and is live for melee swings.** `OpenCombat.take_combat_events` reports each performed swing once - up to the 64 the session keeps waiting for a read, oldest dropped beyond that - and `CombatFeedback` is the only node that drains that queue; it now shares every event it drained onward on `swing_reported`, so the chat, the clip and the floating number are three views of one report. A reported swing is one line and a refused attempt is no line at all, because nothing was reported. `SwingResult.defense_action` - bridge key `defense_action`, spelled `none`, `block` or `dodge` - names the answer that actually ran, so the line never guesses it from damage or prevention. Lines use the scene's node names (`Player`, `Friendly`, `Enemy`), the same number formatting as the floating labels (`combat_text.gd`), the last eight entries newest-at-bottom with older ones dropped, wrapped and clipped rather than scrolled, no fade timer, and no mouse input: the panel is drawn under the popups, and a click that lands on it is a click on the world behind it.

Walks, orders, workbench steps and stamina spends still have no presentation event stream of their own, so the chat says nothing about them.

## Bottom-left action chat — locked

A chat-style text overlay in the **bottom left** of the screen. It is a **debug action feed**, not a combat-only log: it is meant to show what characters are doing as it happens (combat, orders, movement, workbenches, stamina, and later the same shape for other discrete actions).

It is not a second simulation. Refused attempts that C++ never performed must not get a line. Guessing block vs dodge, damage, or a craft result from animation or a snapshot is out.

**Locked — discrete lines.** A line is a thing that happened: an order issued or cancelled, a walk started or arrived, a performed swing and its defense result, a workbench action started or finished, stamina paid as part of that action. No repeating live HP/stamina ticks, and no line when nothing new occurred. Pool values may appear inside the action that changed them.

**Locked — first slice.** The first visible lines are performed melee swings and their defense result (hit / dodge / block). Same overlay. Orders, walks, workbenches and other kinds stay the destination; they do not ship in this slice.

**Locked — combat line.** One performed swing, one line. Name the defense when it was actually attempted. Print remaining damage Y only when Y > 0. Full prevent omits Y.

- No defense attempted, Y > 0: `{attacker} attacks {target} and deals Y damage`
- No defense attempted, Y = 0: `{attacker} attacks {target}` (armour or a zero-offense swing; not a dodge/block)
- Defense attempted, Y > 0: `{attacker} attacks but {target} blocks/dodges and receives Y damage`
- Defense attempted, Y = 0: `{attacker} attacks but {target} blocks/dodges`

Do not guess dodge vs block. The event has to name which defense ran. An undefended hit is not a failed dodge.

**Locked — names.** `{attacker}` and `{target}` are the scene node names: Player, Friendly, Enemy. Empty sheet names are not used.

**Locked — Y.** Remaining damage uses the same precision rule as the floating 3D numbers: enough decimals that a small positive hit is not printed as 0.

**Locked — chrome.** Plain readable log: dark panel, parchment text from the style palette, bottom left. It ignores pointer input so world clicks pass through. No character-sheet frame, drag handle, or close button.

**Locked — history.** Keep the last 8 lines. Newest at the bottom. Older lines drop off. No fade timer, no scrollbar.

## Still Open

Later kinds on the same overlay: orders, walks, workbenches, stamina-as-part-of-action, and anything else discrete. Body part, technique name, and a hide/show toggle were not asked, so they are not in the first slice.
