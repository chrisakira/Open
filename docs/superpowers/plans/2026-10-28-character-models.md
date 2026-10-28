# Character Models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the three visual-spike dummies with assignable Quaternius human models while preserving their existing behavior.

**Architecture:** Keep character control and collision on the existing nodes, and instantiate a Godot `PackedScene` visual under each character. The main scene construction assigns the male visual to the player and female visuals to both standing characters; friendly/enemy labels continue to show faction. No simulation or GDExtension changes are needed.

**Tech Stack:** Godot 4.7.2, GDScript, glTF, existing C++ simulation.

**Spec:** Approved conversation: male player, female ally and enemy; replaceable visuals; existing gameplay and faction distinction; no new walk animation required.

## Global Constraints

- Use the free CC0 Quaternius Universal Base Characters Standard pack, preserving its license and credit.
- Godot controls presentation only; gameplay state remains in the existing C++ classes.
- Do not implement character customization, combat, recruiting, or gameplay systems.
- Do not overwrite or discard existing user changes.

## Review Focus

- Imported glTF and texture dependencies must be present on a clean Godot import.
- Each role must get its intended model independently, including both female instances.
- An alternate valid visual scene must be assignable without affecting collision or gameplay.
- Player movement and turning must not access removed dummy limbs.
- Both standing characters must retain their faction, stationary behavior, and readable colored labels.

---

### Task 1: Import and assign character visuals

**Files:**
- Create: `assets/characters/quaternius/` containing selected Standard glTF models, their referenced `.bin` and image files, and `License_Standard.txt`.
- Create: `scenes/characters/male.tscn` and `scenes/characters/female.tscn` wrapping the imported models with any necessary presentation-only scale and orientation.
- Create: `scripts/tests/character_visuals_test.gd` for a headless scene integration test.
- Modify: `scripts/player.gd`, `scripts/standing_dummy.gd`, and `scripts/main.gd`.
- Modify: `README.md` to document assets, license, model assignments, and absent walk animation.

**Interfaces:**
- Both character scripts accept an independently assigned `PackedScene` visual before entering the scene tree; each attaches one instance beneath a node named `Visual`.
- `scripts/main.gd` assigns the male scene to the player and the female scene to both standing characters.
- Preserve the existing `StandingDummy.setup(faction, level, body_color)` gameplay initialization, or update all callers together if its presentation argument is removed.

- [x] Write a headless Godot integration test that instantiates the main scene, verifies the three assigned visual scenes contain imported `MeshInstance3D` nodes, verifies friendly/enemy faction and `can_move` state, and verifies a different `PackedScene` can be assigned to one character without changing the others. Assert collision shapes remain present.
- [x] Run the test against the current code and confirm it fails because assignable visual scenes are not implemented.
- [x] Copy the male and female `Superhero_*_FullBody.gltf` assets from the pack's `Base Characters/Godot - UE/` directory, their `.bin` files and all referenced textures, plus `License_Standard.txt`. Include selected hairstyles only if they can be positioned correctly. Create the two reusable visual wrapper scenes.
- [x] Instantiate the assigned visuals from the existing character scripts, retain collision and labels, remove assumptions that the old limb nodes exist, and retain movement and turning. Assign visuals in `scripts/main.gd`. Add a tested relaxed standing pose instead of the supplied T-pose.
- [x] Import via `godot --path . --headless --import --quit`, run the new integration test, and run `ctest --test-dir cpp/build --output-on-failure`. Verify zero failures.
- [ ] Launch Godot and inspect the characters, their grounding and scale, the colored labels, and click-to-move. Rendered preview inspected; movement orders tested headlessly, but a manual mouse click has not been verified.
- [x] Update the README with the model source and CC0 license, how to assign a different visual, and the current lack of a walk animation.
