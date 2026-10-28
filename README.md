# Open

Throwaway click-to-move spike. Left click the floor; the dummy walks there.

The walk order is C++. Godot draws the world and sends the click.

The player uses a male model; the friendly and enemy characters use a female
model with green and red labels. Their presentation is assigned independently
using `model_scene` in `scripts/main.gd`. To use a different model, make a
Godot scene for it and assign that scene to the desired character before it is
added to the world (and, for a standing character, before calling `setup`).
The player uses a procedural walking animation and returns to a relaxed pose
when stationary. Models without the optional walking-pose script still work.

The character models and hair are from [Universal Base Characters by
Quaternius](https://quaternius.com/packs/universalbasecharacters.html),
released under CC0. The original license is included at
`assets/characters/quaternius/License_Standard.txt`.

```
./scripts/build_and_run.sh
```

To run the character-presentation integration test after importing assets:

```
godot --path . --headless --import --quit
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
```
