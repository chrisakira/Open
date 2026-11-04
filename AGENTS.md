# AGENTS.md

This is **Open**, a single-player 3D Kenshi-like sandbox. You give orders; characters carry them out. Planned later: the roster and switching who you command, bases, crafting, a switchable 3D / tabletop camera.

This repo is **not** `Project_OPEN`. Do not copy that architecture, docs, or code unless the human asks.

The character sheet is implemented in C++ (`Character` and modules). Click-to-move works. The player and two standing dummies (green friend, red enemy) all use `OpenCharacter`. Standing dummies call `set_can_move(false)` so they never walk. A performed swing provokes: an unordered victim takes the attacker as its attack order and turns to face it (`docs/combat.md` section 1). The player owns the five starter items (C++ setup command), and the scene walks, fights with the typed model, performs techniques, shows held models, swing motion and damage numbers, and carries the character, equipment, technique and workbench panels. Crafting, repair and enchanting run through the coordinator's queue; the scene places the two benches. Jane Doe 1 starts recruitable: the character popup grows a Recruit action wherever the flag says so, the ask walks whoever is commanded into the unarmed melee 1.5, and a yes puts that sheet in the squad (`docs/recruiting.md`). Nobody can be commanded but the player yet - the roster is not built.

## Language split

- **C++** (`cpp/`) owns simulation: move orders, character sheets, anything that should still be true with Godot closed.
- **Godot / GDScript** (`scripts/*.gd`, `scenes/`) owns presentation: camera, meshes, clicks, labels.
- **GDExtension** (`extension/`) is the only place C++ and Godot meet. GDScript cannot call `open::` types directly.

`cpp/` must not include Godot headers or link `godot-cpp`. That is the `open_sim` contract (no automated grep test yet). If a Godot type is leaking into `cpp/include` or `cpp/src`, stop and move it to `extension/`.

Player intent enters as an order (click → destination). Presentation draws the result. Do not make GDScript the source of truth for walk velocity, faction, level, health, strength, or limbs.

## Layout

| Path | What |
|---|---|
| `cpp/include/open/` | Simulation headers |
| `cpp/src/` | Simulation implementation |
| `cpp/tests/` | Headless C++ tests |
| `extension/` | GDExtension (`OpenMoveOrder`, `OpenCharacter`, `OpenCombat`) |
| `extension/open.gdextension` | Loader; library key is `linux.x86_64` |
| `scripts/*.gd` | Godot presentation |
| `scripts/tests/` | Headless Godot tests |
| `scripts/build_and_run.sh` | Build, test, launch |
| `scenes/main.tscn` | Main scene |
| `project.godot` | Godot 4.7 project |
| `docs/` | Design. `docs/README.md` is the index; ownership per file below |
| `docs/superpowers/plans/` | Implementation plans |
| `assets/` | Art packs; keep each pack's license files |

Godot is **4.7.2**. godot-cpp is pinned at **godot-4.5-stable** (FetchContent). Optional: `GODOT_CPP_DIR` for a local checkout.

## Design documents

Each doc owns its domain and points at the others rather than restating them:

- `docs/characters.md` — the sheet: attributes, skills, titles, blessings, limbs, equipment slots. Owns character intent.
- `docs/combat.md` — attack orders, range, swings, attack speed, damage, defense, body parts, combat state and XP.
- `docs/weapons.md` — items: starter equipment, typed damage, techniques, durability, and the workbench actions (craft, repair, enchant).
- `docs/style.md` — visual and interaction style for menus and popups; the rustic/medieval reference implementation is `scripts/character_popup.gd`.
- `docs/combat_log.md` — the bottom-left action chat. Presentation only; it must never become a second simulation.
- `docs/recruiting.md` — how a character joins the player's squad: the recruitable flag, each sheet's own recruit condition, the recruit order and its range, what a yes and a no do, and the only way a member leaves. C++ owns all of it; the roster and switching who you command are not this file's and are not built.

Code/tests win if they disagree with an older paragraph. Respect each file's Current / Locked / Open markers: do not build what is merely Locked, and never what is Open.

## Build and verify

```
./scripts/build_and_run.sh
```

That configures CMake, builds `open_sim` + `libopen.so`, runs `ctest`, then launches Godot.

C++ only (no godot-cpp fetch):

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=OFF
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
```

After any C++ or extension change, **rebuild `libopen.so` then re-import** before running Godot:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
```

A stale extension looks like missing classes (`OpenMoveOrder`, `OpenCharacter`, `OpenCombat`) or old behavior. A skipped import keeps old GDScript method signatures.

Godot headless checks (after import) — run at least the tests covering what you changed; `scripts/tests/` holds them all (sheet, visuals, animation, popups, melee combat, bridges, combat feedback and log, workbench):

```
godot --path . --headless --script scripts/tests/character_sheet_test.gd
godot --path . --headless --script scripts/tests/character_visuals_test.gd
godot --path . --headless --script scripts/tests/melee_combat_test.gd
godot --path . --headless --script scripts/tests/workbench_test.gd
```

New `class_name` GDScript needs that import once or the editor will not see the name.

Do not commit `cpp/build/`, `extension/bin/`, `.godot/`, or `*.so`.

## Rules an agent is likely to break

- Do not put gameplay state only on nodes. Nodes represent C++ state.
- Do not implement walk math, stats, or faction in GDScript if it belongs in `cpp/`.
- Do not add Godot types to `open_sim`.
- Do not treat the visual scene as the finished game. Keep new work small and visible unless the human asks for a system.
- Do not invent world lore, UI, or extra cameras (tabletop) unless asked. Any new UI follows `docs/style.md`. Camera is 3D third person for now.
- Ask when a design choice would change what gets built (combat, recruiting, bases, limbs as gameplay). Do not decide those silently.
- C++ simulation changes need a failing test first, then the code, then `ctest` green.

## Spike facts (do not “clean these up” unless asked)

- Click the floor: the player dummy walks there. C++ `Character::velocity_at` owns the velocity; walk speed is `4.5` times the leg multiplier.
- Green dummy = friendly, red dummy = enemy. Both and the player use `OpenCharacter`.
- Faction ints on the bridge: `0` friendly, `1` enemy.
- Standing dummies call `set_can_move(false)` so they never walk.
- Arrive radius is `0.4`. Movement is horizontal; Y is ignored.

## When something is unclear

Ask. A wrong assumption here becomes the game.
