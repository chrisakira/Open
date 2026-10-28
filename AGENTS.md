# AGENTS.md

This is **Open**, a single-player 3D Kenshi-like sandbox. You give orders; characters carry them out. Planned later: recruit, levels/stats, limb loss, bases, crafting, a switchable 3D / tabletop camera.

This repo is **not** `Project_OPEN`. Do not copy that architecture, docs, or code unless the human asks.

The current tree is a **throwaway visual spike**, not the full game. Click-to-move works. Two standing dummies exist (green friend, red enemy) with C++ character sheets. They cannot walk.

## Language split

- **C++** (`cpp/`) owns simulation: move orders, character sheets, anything that should still be true with Godot closed.
- **Godot / GDScript** (`scripts/*.gd`, `scenes/`) owns presentation: camera, meshes, clicks, labels.
- **GDExtension** (`extension/`) is the only place C++ and Godot meet. GDScript cannot call `open::` types directly.

`cpp/` must not include Godot headers or link `godot-cpp`. That is enforced by the `open_sim` target. If a Godot type is leaking into `cpp/include` or `cpp/src`, stop and move it to `extension/`.

Player intent enters as an order (click → destination). Presentation draws the result. Do not make GDScript the source of truth for walk velocity, faction, level, health, strength, or limbs.

## Layout

| Path | What |
|---|---|
| `cpp/include/open/` | Simulation headers |
| `cpp/src/` | Simulation implementation |
| `cpp/tests/` | Headless C++ tests |
| `extension/` | GDExtension (`OpenMoveOrder`, `OpenCharacter`) |
| `extension/open.gdextension` | Loader; library key is `linux.x86_64` |
| `scripts/*.gd` | Godot presentation |
| `scripts/build_and_run.sh` | Build, test, launch |
| `scenes/main.tscn` | Main scene |
| `project.godot` | Godot 4.7 project |
| `docs/` | Design. `docs/characters.md` owns character intent. Code/tests win if they disagree. |

Godot is **4.7.2**. godot-cpp is pinned at **godot-4.5-stable** (FetchContent). Optional: `GODOT_CPP_DIR` for a local checkout.

## Build and verify

```
./scripts/build_and_run.sh
```

That configures CMake, builds `open_sim` + `libopen.so`, runs `ctest`, then launches Godot.

C++ only:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
```

After any C++ or extension change, **rebuild `libopen.so` before running Godot**. A stale extension looks like missing classes (`OpenMoveOrder`, `OpenCharacter`) or old behavior.

New `class_name` GDScript needs a Godot import once (`godot --path . --headless --import --quit`) or the editor will not see the name.

Do not commit `cpp/build/`, `extension/bin/`, `.godot/`, or `*.so`.

## Rules an agent is likely to break

- Do not put gameplay state only on nodes. Nodes represent C++ state.
- Do not implement walk math, stats, or faction in GDScript if it belongs in `cpp/`.
- Do not add Godot types to `open_sim`.
- Do not treat this spike as the finished game. Keep new work small and visible unless the human asks for a system.
- Do not invent world lore, UI, or extra cameras (tabletop) unless asked. Camera is 3D third person for now.
- Ask when a design choice would change what gets built (combat, recruiting, bases, limbs as gameplay). Do not decide those silently.
- C++ simulation changes need a failing test first, then the code, then `ctest` green.

## Spike facts (do not “clean these up” unless asked)

- Click the floor: the brown dummy walks there. C++ `MoveOrder` owns the velocity.
- Green dummy = friendly character, red dummy = enemy. C++ `Character` owns faction, level, health, strength, limbs, `can_move`.
- Faction ints on the bridge: `0` friendly, `1` enemy.
- Standing dummies call `set_can_move(false)` so they never walk.
- Arrive radius is `0.4`. Movement is horizontal; Y is ignored.

## When something is unclear

Ask. A wrong assumption here becomes the game.
