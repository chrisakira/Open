extends SceneTree

# The join moment: what the player is told when an ask gets an answer.
#
# Three things happen and only three: the answer is reported once, in the chat's own voice; a
# yes-or-no card appears over the character who answered; and the squad plaque grows a name
# because membership grew. Everything here is the *display* of an answer C++ already settled -
# the same sheets the bridge pins, the same sentences the log suite pins - so what this suite
# refuses to let anybody build is a second simulation: no poll that drains, no inference from a
# membership snapshot, no verdict that guesses at a number.
#
# The asks are pressed the way a player presses them: click the living dummy, click Recruit. The
# scene is the issuer, and decision C (2026-11-04) says an issuer reports *its own* answer - so
# an ask ordered straight through the coordinator by another recruiter is exactly the case that
# must stay silent here, and it is built that way on purpose.
#
# The silence cases matter as much as the sentences. An ask that walked away, chose a fight
# instead, or buried its target produced no answer, and a chat that narrated one would be
# inventing an answer nobody gave.
#
# New doors are reached by `has_method` and `call`, so a bubble that has not grown one fails as a
# counted assertion instead of ending the run with the rest of it uncounted.

var failures := 0
var checks := 0
var _viewport_size := Vector2.ZERO

const WORLD_PATH := "res://scenes/main.tscn"
const BUBBLE_PATH := "res://scripts/recruit_bubble.gd"

const JANE_ID := 2
const ENEMY_ID := 3
const PLAYER_ID := 1
const ALWAYS_FAIL := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- fixtures ---------------------------------------------------------------

func _world():
	var world: Node = load(WORLD_PATH).instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	return world


func _free_world(world: Node) -> void:
	world.queue_free()
	await process_frame
	await process_frame


func _log_of(world: Node) -> Control:
	var found: Array = world.find_children("CombatLog", "", true, false)
	_check(found.size() == 1, "the world carries exactly one action chat")
	return null if found.is_empty() else found[0] as Control


func _plaque_of(world: Node) -> Control:
	var found: Array = world.find_children("SquadList", "", true, false)
	_check(found.size() == 1, "the world carries exactly one squad plaque")
	return null if found.is_empty() else found[0] as Control


func _bubble_of(world: Node) -> Control:
	var found: Array = world.find_children("RecruitBubble", "", true, false)
	_check(found.size() == 1, "the world carries exactly one answer bubble")
	if found.is_empty():
		return null
	var bubble := found[0] as Control
	_check(bubble.get_script() != null, "the bubble is built by a script of its own")
	if bubble.get_script() == null:
		return null
	for door in ["bind_view", "open_for", "word", "tick"]:
		_check(bubble.has_method(door), "the answer bubble offers %s()" % door)
	return bubble


func _overlay_of(world: Node) -> Control:
	var found: Array = world.find_children("StatusOverlay", "", true, false)
	_check(found.size() == 1, "the world carries exactly one status overlay")
	return null if found.is_empty() else found[0] as Control


func _lines_of(control: Control) -> PackedStringArray:
	var found := PackedStringArray()
	if control == null:
		return found
	for label in control.find_children("*", "Label", true, false):
		found.append(String(label.text))
	return found


# The sentences the scene may tell about recruiting. A fight that started elsewhere is somebody
# else's line, and this suite is about the recruiting ones: silence means no recruit sentence,
# not no sentence at all.
func _recruit_lines(control: Control) -> PackedStringArray:
	var found := PackedStringArray()
	for line in _lines_of(control):
		if String(line).contains("recruit"):
			found.append(line)
	return found


func _rows_of(plaque: Control) -> PackedStringArray:
	var found := PackedStringArray()
	if plaque == null:
		return found
	for label in plaque.find_children("*", "Label", true, false):
		if String(label.name).begins_with("Member_"):
			found.append(String(label.text))
	return found


func _popup_of(world: Node) -> Control:
	var found: Array = world.find_children("AttackPopup", "", true, false)
	_check(found.size() == 1, "the world carries exactly one action popup")
	return null if found.is_empty() else found[0] as Control


func _button_of(popup: Control, button_name: String) -> Button:
	if popup == null:
		return null
	var found: Array = popup.find_children(button_name, "Button", true, false)
	if found.size() != 1:
		_check(false, "the popup carries exactly one %s, got %d" % [button_name, found.size()])
		return null
	return found[0] as Button


func _click_at(viewport_pos: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = viewport_pos
	motion.global_position = viewport_pos
	root.push_input(motion, true)
	var down := InputEventMouseButton.new()
	down.button_index = MOUSE_BUTTON_LEFT
	down.position = viewport_pos
	down.global_position = viewport_pos
	down.pressed = true
	root.push_input(down, true)
	var up := InputEventMouseButton.new()
	up.button_index = MOUSE_BUTTON_LEFT
	up.position = viewport_pos
	up.global_position = viewport_pos
	up.pressed = false
	root.push_input(up, true)


func _dummy_screen(camera: Camera3D, body: Node) -> Vector2:
	return camera.unproject_position(body.global_position + Vector3(0.0, 0.9, 0.0))


# The player's own ask, pressed through the popup: the only door a player has to it, and the
# reason a display can trust that an ask it saw issued is its own.
func _press_action(world: Node, body: Node, button_name: String) -> bool:
	var camera: Camera3D = world.get("_camera")
	var popup := _popup_of(world)
	if popup == null:
		return false
	_click_at(_dummy_screen(camera, body))
	await process_frame
	if not bool(popup.visible):
		_check(false, "clicking the dummy opened no action popup")
		return false
	var button := _button_of(popup, button_name)
	if button == null or not bool(button.visible):
		_check(false, "the popup does not offer %s" % button_name)
		return false
	_click_at(button.get_global_rect().get_center())
	await process_frame
	_check(not bool(popup.visible), "pressing an action closes the popup")
	return true


# Standing somebody in melee reach of the player, toward the camera: a dummy parked sideways of
# him overlaps his own pick area on screen, and the click meant for her would hit him instead.
func _in_reach_of_player(who: Node) -> void:
	who.global_position = Vector3(0.0, 0.1, 1.2)


# The same reach, between two dummies instead: whoever is asked must be where the asker can
# reach, and a standing dummy is not going to walk anywhere.
func _in_reach_of(who: Node, anchor: Node) -> void:
	who.global_position = anchor.global_position + Vector3(0.0, 0.0, 1.2)


func _far_left(who: Node) -> void:
	who.global_position = Vector3(-14.0, 0.1, 0.0)


func _at(who: Node, at: Vector3) -> void:
	who.global_position = at


func _out_of_every_view(who: Node) -> void:
	# Farther than the overlay's own view distance: the camera cannot show her, so no status
	# view of hers shows - and a card that borrows that gate hides with it.
	who.global_position = Vector3(0.0, 0.1, 120.0)


# --- the join moment -----------------------------------------------------------

func _a_yes_reports_once_in_the_chat_and_says_yes_over_her() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	var plaque := _plaque_of(world)
	if log == null or bubble == null or plaque == null:
		await _free_world(world)
		return
	_in_reach_of_player(jane)
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")), "Jane is asked from the popup")
	await physics_frame
	await process_frame

	# The answer joined her: C++'s decision, pinned already. Here it is only the evidence that
	# the answer was a yes.
	_check(bool(jane.sheet.is_in_squad()), "the yes put her in the squad")
	var lines := _recruit_lines(log)
	_check(lines.size() == 1, "one answer is one recruit line, got %s" % str(lines))
	if lines.size() == 1:
		_check(String(lines[0]) == "John Doe recruited Jane Doe 1",
				"the line reads:\n  want [John Doe recruited Jane Doe 1]\n  got  [%s]"
						% String(lines[0]))
	_check(bool(bubble.visible), "the answer is said over the one who said it")
	_check(String(bubble.call("word")) == "YES", "the word is YES: got [%s]"
			% String(bubble.call("word")))
	_check(_close(float(bubble.modulate.a), 1.0), "and it arrives at full ink, not mid-fade")
	# The plaque re-reads membership on its own drawn frame, so a name it has not picked up
	# yet is a frame away, not a call away: nobody here is asked to refresh anything.
	await process_frame
	await process_frame
	var rows := _rows_of(plaque)
	_check(rows.size() == 2, "the plaque grew the member with no help from this suite: got %s"
			% str(rows))
	if rows.size() == 2:
		_check(String(rows[1]) == "Jane Doe 1", "and her name is hers, not the node's: got [%s]"
				% String(rows[1]))
	_check(int(jane.sheet.get_faction()) == 0, "and the display never re-factioned the recruit")

	for frame in range(10):
		await physics_frame
		await process_frame
	_check(_recruit_lines(log).size() == 1,
			"one answer stays one line while the frames run, got %s" % str(_recruit_lines(log)))
	await _free_world(world)


func _a_no_reports_the_refusal_and_says_no_over_him() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	var plaque := _plaque_of(world)
	if log == null or bubble == null or plaque == null:
		await _free_world(world)
		return
	# The enemy is this scene's refusal: her condition is the one that always fails, and the
	# flag is scene setup exactly where the scene's own setup writes Jane's.
	_check(bool(enemy.sheet.set_recruitable(true)), "the enemy may be asked")
	_check(bool(enemy.sheet.set_recruit_condition(ALWAYS_FAIL)), "and she always says no")
	_in_reach_of_player(enemy)
	await physics_frame
	_check(bool(await _press_action(world, enemy, "RecruitButton")),
			"the enemy is asked from the popup")
	await physics_frame
	await process_frame

	_check(not bool(enemy.sheet.is_in_squad()), "the no seated nobody")
	var lines := _recruit_lines(log)
	_check(lines.size() == 1, "a refusal is still one line, got %s" % str(lines))
	if lines.size() == 1:
		_check(String(lines[0]) == "John Doe failed to recruit John Doe 1",
				"the refusal reads:\n  want [John Doe failed to recruit John Doe 1]\n  got  [%s]"
						% String(lines[0]))
	_check(String(bubble.call("word")) == "NO", "the word is NO: got [%s]"
			% String(bubble.call("word")))
	_check(bool(bubble.visible), "and a refusal is said as openly as a yes")
	_check(_rows_of(plaque).size() == 1, "a no grows the plaque nobody: got %s"
			% str(_rows_of(plaque)))
	await _free_world(world)


func _somebodys_elses_ask_is_its_own_business() -> void:
	var world = await _world()
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var combat = world.get("_combat")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	var plaque := _plaque_of(world)
	if combat == null or log == null or bubble == null or plaque == null:
		await _free_world(world)
		return
	_in_reach_of(enemy, jane)
	await physics_frame
	_check(bool(combat.issue_recruit(ENEMY_ID, JANE_ID)), "the enemy asks Jane through C++")
	await physics_frame
	await process_frame

	# Jane answered *that* ask, and joined - membership is the coordinator's, so the plaque
	# shows her. But this scene never issued the ask, so its issuer has no answer of its own
	# to report: no line, no card. A watcher that reported every stamp it happened to see
	# would be telling the player about somebody else's order.
	_check(bool(jane.sheet.is_in_squad()), "the answer still joined her, C++'s answer as ever")
	_check(int(enemy.sheet.get_recruit_target_id()) == 0, "and her asker was stood down")
	_check(_recruit_lines(log).is_empty(),
			"the player saw no answer to an ask it never issued: got %s"
					% str(_recruit_lines(log)))
	_check(not bool(bubble.visible), "and no card floats over a stranger's answer")
	var rows := _rows_of(plaque)
	_check(rows.size() == 2, "the plaque still mirrors the squad it reads: got %s" % str(rows))
	await _free_world(world)


func _a_walked_away_ask_is_silent() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var combat = world.get("_combat")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	if combat == null or log == null or bubble == null:
		await _free_world(world)
		return
	_far_left(jane)
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")),
			"Jane is asked from across the floor")
	# A new walk is the cancel docs/recruiting.md locked: the ask goes, no attempt runs. This is
	# the coordinator's own door, the one the scene's floor click uses.
	_check(bool(combat.issue_move(PLAYER_ID, Vector3(8.0, 0.0, 8.0))), "a walk is ordered instead")
	for frame in range(24):
		await physics_frame
		await process_frame
	_check(_recruit_lines(log).is_empty(), "the broken ask produced no sentence: got %s"
			% str(_recruit_lines(log)))
	_check(not bool(bubble.visible), "and no verdict over anybody")
	_check(bool(jane.sheet.is_recruitable()), "a walk-away never spent her flag")
	_check(not bool(jane.sheet.is_in_squad()), "and nobody joined")
	await _free_world(world)


func _a_fight_chosen_instead_is_silent() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	if log == null or bubble == null:
		await _free_world(world)
		return
	_far_left(jane)
	await physics_frame
	_at(enemy, Vector3(3.0, 0.1, -6.0))
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")),
			"Jane is asked from across the floor")
	_check(bool(await _press_action(world, enemy, "AttackButton")), "and a fight is chosen instead")
	for frame in range(12):
		await physics_frame
		await process_frame
	_check(_recruit_lines(log).is_empty(),
			"choosing a fight is not an answer, so nothing was said: got %s"
					% str(_recruit_lines(log)))
	_check(not bool(bubble.visible), "nor was any verdict owed to anybody")
	_check(bool(jane.sheet.is_recruitable()), "the ask that broke spent no flag")
	_check(not bool(jane.sheet.is_in_squad()), "and nobody joined on the way")
	await _free_world(world)


func _a_target_that_dies_first_is_silent() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var log := _log_of(world)
	var bubble := _bubble_of(world)
	if log == null or bubble == null:
		await _free_world(world)
		return
	_far_left(jane)
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")),
			"Jane is asked from across the floor")
	_check(bool(jane.sheet.apply_damage(1, 500.0)), "and the sheet walked to dies")
	for frame in range(12):
		await physics_frame
		await process_frame
	_check(_recruit_lines(log).is_empty(), "a corpse answers nothing, so nothing was told: got %s"
			% str(_recruit_lines(log)))
	_check(not bool(bubble.visible), "and no card is owed to a character who is gone")
	await _free_world(world)


# --- the card's own life -------------------------------------------------------

func _the_watch_holds_then_fades_and_says_nothing_after() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var bubble := _bubble_of(world)
	if bubble == null:
		await _free_world(world)
		return
	_in_reach_of_player(jane)
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")), "Jane is asked in reach")
	await physics_frame
	await process_frame
	_check(bool(bubble.visible), "the yes is being said")
	# The hold is the human's, flagged 2026-11-04: about a second of a word anybody can finish
	# reading, then most of a second of ink going away, and no movement besides following her.
	bubble.call("tick", 0.5)
	_check(bool(bubble.visible) and _close(float(bubble.modulate.a), 1.0),
			"half a second in, the word still stands at full ink")
	bubble.call("tick", 0.6)
	_check(bool(bubble.visible), "a tenth of a second past the hold is not yet the end")
	var started := float(bubble.modulate.a)
	_check(started < 0.999 and started > 0.0,
			"the fade has begun without the card vanishing at once: got %f" % started)
	bubble.call("tick", 0.2)
	var running := float(bubble.modulate.a)
	_check(running < started and running > 0.0,
			"the ink runs out on the way, not in one step: %f then %f" % [started, running])
	bubble.call("tick", 0.8)
	_check(not bool(bubble.visible), "and after the fade it is gone: alpha %f"
			% float(bubble.modulate.a))
	_check(String(bubble.call("word")) == "", "a card that left takes its word with it")
	_check(bool(jane.sheet.is_in_squad()), "and going away never took the join back")
	await _free_world(world)


func _the_card_stacks_over_the_plate_and_obeys_the_gate() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var bubble := _bubble_of(world)
	var overlay := _overlay_of(world)
	if bubble == null or overlay == null:
		await _free_world(world)
		return
	for door in ["head_anchor", "status_visible"]:
		_check(overlay.has_method(door),
				"the overlay lends its %s, so the card borrows one rule instead of a second"
						% door)
	_in_reach_of_player(jane)
	await physics_frame
	_check(bool(await _press_action(world, jane, "RecruitButton")), "Jane is asked in reach")
	await physics_frame
	await process_frame
	_check(bool(bubble.visible), "the card is up")
	var plate: Array = overlay.find_children("WorldStatus_%d" % JANE_ID, "", true, false)
	_check(plate.size() == 1, "the answered character has an overhead plate to stack over")
	if plate.size() == 1:
		var plate_rect: Rect2 = (plate[0] as Control).get_global_rect()
		_check(bubble.get_global_rect().end.y <= plate_rect.position.y + 0.5,
				"the card hangs above the plate, never over it: %s against %s" % [
						str(bubble.get_global_rect()), str(plate_rect)])
	var wanted := camera.unproject_position(jane.global_position + Vector3(0.0, 2.3, 0.0))
	_check(_close(bubble.position.x + bubble.size.x / 2.0, wanted.x, 1.0),
			"centred on the anchor the overlay uses: card %f, anchor %f" % [
					bubble.position.x + bubble.size.x / 2.0, wanted.x])
	# And it hides when the camera cannot show her, without losing the verdict it is saying.
	_out_of_every_view(jane)
	await process_frame
	await process_frame
	_check(not bool(bubble.visible), "out of range the card hides instead of guessing a place")
	_check(String(bubble.call("word")) == "YES", "and the answer it holds is still the one it was")
	_in_reach_of_player(jane)
	await physics_frame
	await process_frame
	await process_frame
	_check(bool(bubble.visible), "and in range again it shows the word it was already saying")
	await _free_world(world)


func _the_bubble_is_click_through_and_hangs_under_the_popups() -> void:
	var world = await _world()
	var bubble := _bubble_of(world)
	if bubble == null:
		await _free_world(world)
		return
	var controls: Array = [bubble]
	for child in bubble.find_children("*", "Control", true, false):
		controls.append(child)
	_check(controls.size() > 1, "the card has a surface and a word, not one bare control")
	for control in controls:
		_check(int(control.mouse_filter) == int(Control.MOUSE_FILTER_IGNORE),
				"%s never eats a click" % String(control.name))
	_check(bubble.find_children("*", "Button", true, false).is_empty()
			&& bubble.find_children("*", "LineEdit", true, false).is_empty()
			&& bubble.find_children("*", "TextureButton", true, false).is_empty(),
			"and it offers no control at all, so there is nothing to dismiss and nothing to ask"
			+ " again")
	var layer: Node = bubble.get_parent()
	_check(layer is CanvasLayer, "the bubble lives on the HUD canvas layer")
	var above := 0
	for sibling_name in ["CharacterPopup", "AttackPopup", "TechniquePopup", "EquipmentPopup",
			"WorkbenchPopup", "WeaponPopup"]:
		var found: Array = world.find_children(sibling_name, "", true, false)
		var popup: Node = null if found.is_empty() else found[0]
		if popup != null and popup.get_parent() == layer and popup.get_index() > bubble.get_index():
			above += 1
	_check(above == 6, "every popup still draws over the verdict, saw %d" % above)
	await _free_world(world)


# --- the run ------------------------------------------------------------------

func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	_viewport_size = root.get_visible_rect().size

	await _case("a yes, said once and said over her", 11,
			_a_yes_reports_once_in_the_chat_and_says_yes_over_her)
	await _case("a no, said as openly", 8, _a_no_reports_the_refusal_and_says_no_over_him)
	await _case("somebody else's ask is its own business", 6,
			_somebodys_elses_ask_is_its_own_business)
	await _case("a walked-away ask is silent", 5, _a_walked_away_ask_is_silent)
	await _case("a fight chosen instead is silent", 5, _a_fight_chosen_instead_is_silent)
	await _case("a target that dies first is silent", 4, _a_target_that_dies_first_is_silent)
	await _case("hold, fade, then nothing", 10,
			_the_watch_holds_then_fades_and_says_nothing_after)
	await _case("stacks over the plate, obeys the gate", 9,
			_the_card_stacks_over_the_plate_and_obeys_the_gate)
	await _case("click-through under the popups", 10,
			_the_bubble_is_click_through_and_hangs_under_the_popups)

	_check(checks > 70, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Recruit answer display: all checks passed")
	quit(1 if failures else 0)
