# Open

Throwaway click-to-move spike. Left click the floor; the dummy walks there.

Press **I** to open or close the character popup, or use its × button to close
it. Drag its header to move it. It shows the player's effective attributes and
grouped skills, including any chosen specializations, and refreshes while open. Resource pools are not
shown. The game continues running, and clicks outside the popup still move the
player.

Left click a living character, friendly or enemy, and a small popup opens beside
it with one action: **Attack**. Attack orders the player to walk to that
character and swing on C++'s cooldown until the fight ends; **Escape** or a click
elsewhere closes the popup, and a click on the floor cancels the attack and moves
the player instead. Clicking the player or a dead character does nothing. Clicking
a character never orders a move, and the popup only offers the action: whether the
target is still valid, how far it is, and what a swing costs are all decided in
C++. Unarmed, both dummies start taking `0.04` damage per hit at level 1, so the
rounded HP label barely moves; that is intended, not a bug to tune away.

The simulation is C++ (`cpp/`): one `Character` type owns attributes, level,
blessing, pools, body-part health, skills, titles, equipment and the move and
attack orders. A `CombatSession` resolves target ids against the characters the
scene registers, advances each sheet's attack and defend clocks, resolves swings,
and regenerates each living sheet once. Godot publishes positions, sends intent by
id, and steps that session once per physics frame; the actors never tick combat or
regeneration themselves. Walk speed (`4.5` healthy, halved by one dead leg, zero with both legs or
when dead) is computed in C++, not in GDScript. The player and both standing
characters use the same sheet; NPCs draw their blessing from a seed.

The player uses a male model; the friendly and enemy characters use a female
model with green and red labels. Their presentation is assigned independently
using `model_scene` in `scripts/main.gd`. To use a different model, make a
Godot scene for it and assign that scene to the desired character before it is
added to the world (and, for a standing character, before calling `setup`).
The player uses the animation library's normal walk and folded-arms idle as a
visual test; the standing characters use the idle. Models without the optional
animation script still work. To try different clips, open
`scenes/characters/male.tscn` or `scenes/characters/female.tscn` in Godot and
change the root node's `Idle Animation` or `Walk Animation` property. Use names
from either imported library in `assets/animations/` (for example `Jog_Fwd` or
`Zombie_Walk_Fwd`); Godot removes the `_Loop` suffix on import. If a name exists
in both libraries, library 2 takes precedence. The selected clips are loaded when the scene starts. These
in-place animations are presentation only; movement remains controlled by C++.

The player's blessing is chosen by hand for now: set `player_blessing` on the
root of `scenes/main.tscn` (`0` is Strength). Character creation will supply it
later; there is no selection UI.

The character models and hair are from [Universal Base Characters by
Quaternius](https://quaternius.com/packs/universalbasecharacters.html),
released under CC0. The original license is included at
`assets/characters/quaternius/License_Standard.txt`.
The animations are from Quaternius's [Universal Animation Library](https://quaternius.com/packs/universalanimationlibrary.html)
and [Universal Animation Library 2](https://quaternius.com/packs/universalanimationlibrary2.html),
also CC0; their licenses are included in their respective `assets/animations/` directories.

```
./scripts/build_and_run.sh
```

To verify:

```
cmake -S cpp -B cpp/build -DOPEN_BUILD_GDEXTENSION=ON
cmake --build cpp/build -j"$(nproc)"
ctest --test-dir cpp/build --output-on-failure
godot --path . --headless --import --quit
godot --path . --headless --script res://scripts/tests/melee_combat_test.gd
godot --path . --headless --script res://scripts/tests/combat_bridge_test.gd
godot --path . --headless --script res://scripts/tests/character_popup_test.gd
godot --path . --headless --script res://scripts/tests/character_sheet_test.gd
godot --path . --headless --script res://scripts/tests/character_visuals_test.gd
godot --path . --headless --script res://scripts/tests/character_animation_test.gd
```

`character_sheet_test.gd` drives the bridge directly and `combat_bridge_test.gd`
drives the combat coordinator; `character_visuals_test.gd`
checks the scene: models, collision, poses, stationary NPCs, a real floor click,
and the health label. `melee_combat_test.gd` clicks the dummies for real: picking
a living character opens the Attack popup and nothing else, the popup's button is
never a floor click, Escape or an outside click dismisses it, and a confirmed
attack approaches, stops in reach, swings again on its own, and cancels on the
next floor click.
