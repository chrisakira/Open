# Open

Throwaway click-to-move spike. Left click the floor; the dummy walks there.

The simulation is C++ (`cpp/`): one `Character` type owns attributes, level,
blessing, pools, body-part health, skills, titles, equipment and the move order.
Godot draws the world, sends the click, and forwards one regen tick per physics
frame. Walk speed (`4.5` healthy, halved by one dead leg, zero with both legs or
when dead) is computed in C++, not in GDScript. The player and both standing
characters use the same sheet; NPCs draw their blessing from a seed.

The player uses a male model; the friendly and enemy characters use a female
model with green and red labels. Their presentation is assigned independently
using `model_scene` in `scripts/main.gd`. To use a different model, make a
Godot scene for it and assign that scene to the desired character before it is
added to the world (and, for a standing character, before calling `setup`).
The player uses a procedural walking animation and returns to a relaxed pose
when stationary. Models without the optional walking-pose script still work.

The player's blessing is chosen by hand for now: set `player_blessing` on the
root of `scenes/main.tscn` (`0` is Strength). Character creation will supply it
later; there is no selection UI.

The character models and hair are from [Universal Base Characters by
Quaternius](https://quaternius.com/packs/universalbasecharacters.html),
released under CC0. The original license is included at
`assets/characters/quaternius/License_Standard.txt`.

```
./scripts/build_and_run.sh
```

To verify:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
godot --path . --headless --script res://scripts/tests/character_sheet_test.gd
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
```

`character_sheet_test.gd` drives the bridge directly; `character_visuals_test.gd`
checks the scene: models, collision, poses, stationary NPCs, a real floor click,
and the health label.
