extends SceneTree

# The bottom-left action chat: one truthful line per performed melee swing.
#
# The chat is presentation, so everything asserted here is about *wording and shape*: which
# words a given event produces, how many lines survive, that the panel cannot eat a click, and
# that the number in the sentence is the same number the floating label prints. What a swing is
# worth, which defence ran and whether anything was prevented are C++'s, already pinned in
# `combat_bridge_test`; the dictionaries below are presentation fixtures in the shape the bridge
# produces, never stand-ins for a simulation result.
#
# The control is reached by loading its path and duck-typing the calls, so a missing script or a
# missing method is an assertion in this suite rather than a parse cascade that ends the run
# without counting anything.

var failures := 0
# A runtime error aborts the rest of a case, and a suite that only counts failures could then be
# quietly green about nothing. Every case is asked to reach a floor.
var checks := 0

const LOG_PATH := "res://scripts/combat_log.gd"
const TEXT_PATH := "res://scripts/combat_text.gd"
const FEEDBACK_PATH := "res://scripts/combat_feedback.gd"
const MAX_LINES := 8

# The six sentences the first slice owns, in the plan's order: none/block/dodge by whether any
# damage got through.
const SENTENCES := [
	["none", 3.8, "Player attacks Enemy and deals 3.8 damage"],
	["none", 0.0, "Player attacks Enemy"],
	["block", 1.5, "Player attacks but Enemy blocks and receives 1.5 damage"],
	["block", 0.0, "Player attacks but Enemy blocks"],
	["dodge", 1.5, "Player attacks but Enemy dodges and receives 1.5 damage"],
	["dodge", 0.0, "Player attacks but Enemy dodges"],
]


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- fixtures ------------------------------------------------------------------

func _log():
	var script: Script = load(LOG_PATH)
	if script == null:
		_check(false, "the action chat exists at %s" % LOG_PATH)
		return null
	var control = script.new()
	if control == null:
		_check(false, "the action chat instantiates")
		return null
	for method in ["bind_actors", "append_swing"]:
		_check(control.has_method(method), "the action chat offers %s()" % method)
	if not control.has_method("append_swing"):
		return null
	return control


func _actor(node_name: String, id: int) -> StandingDummy:
	# A real actor node type, with a real configured sheet behind it. The sheet is left
	# unnamed on purpose: the chat prints scene names, and an empty sheet name is the case
	# that would expose a listener reading the sheet instead.
	var actor := StandingDummy.new()
	actor.name = node_name
	actor.setup(0, 1, id, id * 7 + 1)
	return actor


func _event(actor_id: int, target_id: int, defense: String, damage: float) -> Dictionary:
	# The production key set, so a chat that reads a field the bridge does not send fails
	# here rather than in the scene.
	var event := {
		"actor_id": actor_id, "target_id": target_id, "part": 1, "damage": damage,
		"prevented": 0.0, "attack_cooldown": 0.5, "nullified": false, "technique_id": 0,
		"attacker_item": 0, "defender_item": 0,
	}
	if defense != "<absent>":
		event["defense_action"] = defense
	return event


func _lines_of(control) -> PackedStringArray:
	var found := PackedStringArray()
	for label in control.find_children("*", "Label", true, false):
		found.append(String(label.text))
	return found


func _controls_of(control) -> Array:
	var found: Array = [control]
	for child in control.find_children("*", "Control", true, false):
		found.append(child)
	return found


func _stage(control, viewport_size: Vector2i = Vector2i(1280, 720)) -> SubViewport:
	# A viewport the test owns. The root window cannot be resized from a headless script - the
	# size property moves while the canvas stays put - and a SubViewport really does resize, so
	# "the panel fits a smaller window" is checked against a window that really got smaller.
	var view := SubViewport.new()
	view.size = viewport_size
	root.add_child(view)
	var layer := CanvasLayer.new()
	layer.name = "TestHud"
	view.add_child(layer)
	layer.add_child(control)
	return view


func _unstage(view: Node) -> void:
	view.free()
	await process_frame
	await process_frame


# --- the six sentences ---------------------------------------------------------

func _every_outcome_is_one_true_sentence() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	for index in range(SENTENCES.size()):
		var want := String(SENTENCES[index][2])
		log.append_swing(_event(1, 2, String(SENTENCES[index][0]), float(SENTENCES[index][1])))
		var lines := _lines_of(log)
		_check(lines.size() == index + 1, "%s with %s damage is one line, got %d: %s" % [
				String(SENTENCES[index][0]), str(SENTENCES[index][1]), lines.size(), str(lines)])
		if lines.size() == index + 1:
			_check(String(lines[index]) == want,
					"the sentence for %s with %s damage reads:\n  want [%s]\n  got  [%s]" % [
							String(SENTENCES[index][0]), str(SENTENCES[index][1]), want,
							String(lines[index])])
	_uncheck_actors(player, enemy)
	player.free()
	enemy.free()
	log.free()


func _a_defence_worth_nothing_is_still_named() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	# A 0% buckler: the block ran, prevented nothing, and the blow arrived whole. The wording
	# has to keep the block - "prevented 0" is not evidence that nobody defended.
	log.append_swing(_event(1, 2, "block", 0.04))
	var lines := _lines_of(log)
	_check(lines.size() == 1, "a zero-prevention block is one line")
	if lines.size() == 1:
		_check(String(lines[0]).contains("blocks"),
				"a block that prevented nothing still says blocks: got [%s]" % String(lines[0]))
		_check(not String(lines[0]).contains("dodge"), "and never says dodges")
	# A partially mitigated block, and a half-paid dodge: the defence ran and some arrived.
	log.append_swing(_event(1, 2, "block", 1.52))
	log.append_swing(_event(1, 2, "dodge", 0.5))
	lines = _lines_of(log)
	_check(lines.size() == 3, "three swings are three lines")
	if lines.size() == 3:
		_check(String(lines[1]).contains("blocks and receives 1.52 damage"),
				"a partly stopped blow says what got through: got [%s]" % String(lines[1]))
		_check(String(lines[2]).contains("dodges and receives 0.5 damage"),
				"a half-paid dodge says what got through: got [%s]" % String(lines[2]))
	_uncheck_actors(player, enemy)
	player.free()
	enemy.free()
	log.free()


func _a_zero_without_a_defence_refuses_to_invent_one() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	# Armour soaked the blow to nothing with no defence running. The line must be true about
	# that without naming a cause the event does not report, and must not read as a defence.
	log.append_swing(_event(1, 2, "none", 0.0))
	var lines := _lines_of(log)
	_check(lines.size() == 1, "an armour-zero blow is still one line")
	if lines.size() == 1:
		_check(String(lines[0]) == "Player attacks Enemy",
				"a blow with no defence and no damage claims neither: got [%s]" % String(lines[0]))
	# A defence that turned everything aside: named, and with no damage figure.
	log.append_swing(_event(1, 2, "dodge", 0.0))
	lines = _lines_of(log)
	if lines.size() == 2:
		_check(String(lines[1]) == "Player attacks but Enemy dodges",
				"a full prevent names the dodge and omits the amount: got [%s]" % String(lines[1]))
	# Missing or unrecognised: neutral, never a guess.
	log.append_swing(_event(1, 2, "<absent>", 2.0))
	log.append_swing(_event(1, 2, "parried", 2.0))
	lines = _lines_of(log)
	_check(lines.size() == 4, "a legacy or unknown event is still one line each")
	if lines.size() == 4:
		_check(String(lines[2]) == "Player attacks Enemy and deals 2 damage",
				"an event with no defense_action reads as no defence: got [%s]" % String(lines[2]))
		_check(String(lines[3]) == "Player attacks Enemy and deals 2 damage",
				"an unrecognised defense_action reads as no defence: got [%s]" % String(lines[3]))
	_uncheck_actors(player, enemy)
	player.free()
	enemy.free()
	log.free()


func _names_come_from_the_scene_and_stay_plain_text() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var ally := _actor("Friendly", 2)
	log.bind_actors([player, ally])
	log.append_swing(_event(1, 2, "none", 1.0))
	var lines := _lines_of(log)
	_check(lines.size() == 1, "one swing, one line")
	if lines.size() == 1:
		_check(String(lines[0]) == "Player attacks Friendly and deals 1 damage",
				"the actors are the node names, never the empty sheet names: got [%s]"
				% String(lines[0]))
	# An id nobody in the world owns, and an id whose actor has been freed: both print the
	# id rather than borrowing another character's name.
	var doomed := _actor("Doomed", 3)
	log.bind_actors([player, ally, doomed])
	doomed.free()
	log.append_swing(_event(3, 999, "none", 0.0))
	lines = _lines_of(log)
	if lines.size() == 2:
		_check(String(lines[1]) == "Character 3 attacks Character 999",
				"a freed attacker and an unknown target print their ids: got [%s]" % String(lines[1]))
	# A name that spells markup stays markup-free text. (A node name cannot hold a "/", so the
	# tag is written the way a scene really would.)
	var tricky := _actor("[b]Bold", 4)
	log.bind_actors([player, tricky])
	log.append_swing(_event(4, 1, "dodge", 0.0))
	lines = _lines_of(log)
	if lines.size() == 3:
		_check(String(lines[2]) == "[b]Bold attacks but Player dodges",
				"BBCode-looking names are printed literally: got [%s]" % String(lines[2]))
	_check(log.find_children("*", "RichTextLabel", true, false).is_empty(),
			"and the chat has no rich-text surface that could read them as markup")
	player.free()
	ally.free()
	tricky.free()
	log.free()


# --- the number in the sentence ------------------------------------------------

func _the_amount_is_the_number_the_float_prints() -> void:
	var text_script: Script = load(TEXT_PATH)
	if text_script == null:
		_check(false, "the shared formatter exists at %s" % TEXT_PATH)
		return
	for method in ["damage", "amount"]:
		_check(text_script.has_script_method(method), "the shared formatter offers %s()" % method)
	var feedback = load(FEEDBACK_PATH).new()
	if feedback == null:
		_check(false, "the floating-number formatter is still reachable")
		return
	var values := [0.0, 0.001, 0.00000001, 0.0001, 0.04 / 1.05, 1.5, 3.8, 12.0, 900.0, 12345.678]
	for value in values:
		var shared := String(text_script.damage(value))
		var float_text := String(feedback.call("_damage_text", value))
		_check(shared == float_text,
				"the shared formatter prints %s exactly as the floating label does: [%s] vs [%s]"
						% [str(value), shared, float_text])
		if value > 0.0:
			_check(shared != "0" and float(shared) > 0.0,
					"a positive amount is never displayed as nothing: %s -> [%s]" % [str(value), shared])
			_check(String(text_script.amount(value)) != "0"
					&& float(String(text_script.amount(value))) > 0.0,
					"a positive amount survives the sentence form too: got [%s]"
							% String(text_script.amount(value)))
	# The sentence uses the prose form, so a whole amount is not left holding a bare point.
	var log = _log()
	if log != null:
		var player := _actor("Player", 1)
		var enemy := _actor("Enemy", 2)
		log.bind_actors([player, enemy])
		log.append_swing(_event(1, 2, "none", 12.0))
		var lines := _lines_of(log)
		if lines.size() == 1:
			_check(String(lines[0]) == "Player attacks Enemy and deals 12 damage",
					"a whole amount reads as a number, not a dangling point: got [%s]"
					% String(lines[0]))
		log.append_swing(_event(1, 2, "none", 0.00000001))
		lines = _lines_of(log)
		if lines.size() == 2:
			_check(String(lines[1]).ends_with(" damage") and not String(lines[1]).contains(" 0 "),
					"a ten-millionth of damage still reads as a positive amount: got [%s]"
					% String(lines[1]))
		player.free()
		enemy.free()
		log.free()
	feedback.free()


# --- history, chrome and input -------------------------------------------------

func _the_chat_keeps_the_last_eight_lines() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	for index in range(1, 11):
		log.append_swing(_event(1, 2, "none", float(index)))
	var lines := _lines_of(log)
	_check(lines.size() == MAX_LINES, "ten swings leave %d lines, got %d" % [MAX_LINES, lines.size()])
	var wanted := PackedStringArray()
	for index in range(3, 11):
		wanted.append("Player attacks Enemy and deals %d damage" % index)
	var got := PackedStringArray()
	for line in lines:
		got.append(line)
	_check(got == wanted, "the lines that survive are events 3 to 10, oldest first:\n  got %s" % str(got))
	# A wrap is one entry, not several: a long pair of names must not add lines.
	var long_a := _actor("A".repeat(40), 7)
	var long_b := _actor("B".repeat(40), 8)
	log.bind_actors([long_a, long_b])
	var before := _lines_of(log).size()
	log.append_swing(_event(7, 8, "block", 4.0))
	var after := _lines_of(log)
	_check(after.size() == MAX_LINES, "a wrapped entry is still one entry, so the panel holds %d"
			% MAX_LINES)
	_check(after.size() == before, "and one more swing is one more entry than a drop, not %d lines"
			% (before + 2))
	_check(String(after[after.size() - 1]).contains("but " + "B".repeat(40) + " blocks"),
			"the newest wrapped entry is the one at the bottom: got [%s]"
			% String(after[after.size() - 1]))
	# Nothing about a line disappears on a timer.
	for frame in range(10):
		await process_frame
	_check(_lines_of(log).size() == MAX_LINES, "no line fades out while time passes")
	_check(_labels_untouched(log), "and no line dims itself away")
	_check(log.find_children("*", "Timer", true, false).is_empty(), "the chat owns no timer")
	_check(log.find_children("*", "Tween", true, false).is_empty(), "nor a tween that hides one")
	_check(log.find_children("*", "ScrollContainer", true, false).is_empty()
			&& log.find_children("*", "ScrollBar", true, false).is_empty(),
			"long content wraps or clips, it never grows a scrollbar")
	_check(log.find_children("*", "Button", true, false).is_empty()
			&& log.find_children("*", "LineEdit", true, false).is_empty(),
			"the chat offers no control a click could turn into an order")
	player.free()
	enemy.free()
	long_a.free()
	long_b.free()
	log.free()


func _labels_untouched(control) -> bool:
	for label in control.find_children("*", "Label", true, false):
		if not _close(float(label.modulate.a), 1.0) or not _close(float(label.self_modulate.a), 1.0):
			return false
	return true


func _the_panel_never_eats_a_click_and_never_leaves_the_screen() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	for index in range(MAX_LINES):
		log.append_swing(_event(1, 2, "none", float(index + 1)))
	var view := _stage(log)
	await process_frame
	await process_frame
	_check(int(log.mouse_filter) == int(Control.MOUSE_FILTER_IGNORE),
			"the chat itself ignores the mouse: got %d" % int(log.mouse_filter))
	for control in _controls_of(log):
		_check(int(control.mouse_filter) == int(Control.MOUSE_FILTER_IGNORE),
				"%s ignores the mouse too, so a click reaches the world" % String(control.name))
	_in_bounds(log, view, "at 1280x720")
	view.size = Vector2i(720, 420)
	await process_frame
	await process_frame
	_in_bounds(log, view, "at a smaller 720x420 viewport")
	view.size = Vector2i(1280, 720)
	await process_frame
	_in_bounds(log, view, "back at 1280x720")
	_unstage(view)
	player.free()
	enemy.free()


# Nothing about the chat may leave the screen, crowd the fight out, or sit anywhere but the
# corner the spec locked - at either window size, and for every control inside it.
func _in_bounds(control, view: SubViewport, where: String) -> void:
	var rect_size: Vector2 = view.get_visible_rect().size
	var screen := Rect2(Vector2.ZERO, rect_size)
	var outer: Rect2 = control.get_global_rect()
	var centre := outer.get_center()
	_check(outer.position.x >= 0.0 and outer.end.x <= screen.size.x + 0.5,
			"the panel stays inside the screen horizontally %s: %s in %s" % [where, str(outer),
					str(screen)])
	_check(outer.position.y >= 0.0 and outer.end.y <= screen.size.y + 0.5,
			"and vertically %s: %s in %s" % [where, str(outer), str(screen)])
	_check(outer.size.x <= screen.size.x * 0.6 and outer.size.y <= screen.size.y * 0.6,
			"and never past an honest share of it %s: %s" % [where, str(outer.size)])
	_check(centre.x < screen.size.x * 0.5 and centre.y > screen.size.y * 0.5,
			"and it sits in the bottom left %s: %s of %s" % [where, str(outer), str(screen)])
	_check(bool(control.clip_contents),
			"the panel clips what will not fit in it, so an overflow cannot paint over the world")
	for child in control.find_children("*", "Control", true, false):
		var rect: Rect2 = child.get_global_rect()
		_check(rect.position.x >= -0.5 and rect.end.x <= screen.size.x + 0.5,
				"the %s never spills sideways %s: %s" % [String(child.name), where, str(rect)])
	# The line worth reading is the newest one, so it is the one that must be on screen: a panel
	# that grew its entries outwards would push exactly this off the bottom.
	var lines: Array = control.find_children("*", "Label", true, false)
	if lines.size() > 0:
		var newest: Control = lines[lines.size() - 1]
		var newest_rect: Rect2 = newest.get_global_rect()
		_check(screen.has_point(newest_rect.get_center()),
				"the newest entry is on screen %s: %s in %s" % [where, str(newest_rect), str(screen)])
		_check(outer.has_point(newest_rect.get_center()),
				"and inside the panel that reports it %s: %s in %s" % [where, str(newest_rect),
						str(outer)])


# --- the drain is somebody else's ----------------------------------------------

func _the_chat_holds_no_second_drain_of_its_own() -> void:
	var log = _log()
	if log == null:
		return
	# The chat is handed events. If it ever reached for the coordinator itself there would be
	# two drains of one queue, and the swing would be shown once and animated once at best.
	var text_script: Script = load(TEXT_PATH)
	_check(text_script != null, "the shared formatter is there to preload")
	var script: Script = log.get_script()
	if script == null:
		_check(false, "the chat is a script of its own")
		return
	for method in script.get_script_method_list():
		var declared := String(method["name"])
		_check(declared != "take_combat_events" and declared != "_physics_process",
				"the chat neither drains a queue nor polls on its own clock: found %s" % declared)
	var layer := _stage(log)
	await process_frame
	await process_frame
	_check(log.get_child_count() >= 0, "the chat survives a real frame on a canvas layer")
	_unstage(layer)


# The panel measures its entries, so it has to be able to measure them again: narrow the window
# until every line wraps, and the same two swings need twice the room to say the same things.
func _the_panel_regrows_when_the_entries_wrap() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	log.append_swing(_event(1, 2, "none", 3.8))
	log.append_swing(_event(1, 2, "block", 1.5))
	var view := _stage(log, Vector2i(1280, 720))
	await process_frame
	await process_frame
	var before: Rect2 = log.get_global_rect()
	_assert_entries_whole(log, "before the window narrows")
	view.size = Vector2i(760, 720)
	await process_frame
	await process_frame
	var after: Rect2 = log.get_global_rect()
	_check(after.size.y > before.size.y,
			"a narrower window wraps the entries and the panel answers by growing: %s -> %s" % [
					str(before.size), str(after.size)])
	_assert_entries_whole(log, "after the entries wrapped")
	_unstage(view)
	player.free()
	enemy.free()


# Entries that would fit must not be clipped: the panel is allowed to clip only what genuinely
# has no room in it.
func _assert_entries_whole(log, where: String) -> void:
	var panel: Rect2 = log.get_global_rect()
	var labels: Array = log.find_children("*", "Label", true, false)
	_check(labels.size() == 2, "both swings are on the panel %s: %d lines" % [where, labels.size()])
	for label in labels:
		var rect: Rect2 = (label as Control).get_global_rect()
		_check(panel.grow(0.5).encloses(rect),
				"the whole of a line is inside the panel %s: %s in %s" % [where, str(rect), str(panel)])


# The chat is handed the very dictionary the clip and the floating number are drawn from, so it
# may read it and nothing else: an edit dressed as formatting - a normalised name, a tidied key -
# would reach the next view as though C++ had said it.
func _a_reported_event_comes_through_untouched() -> void:
	var log = _log()
	if log == null:
		return
	var player := _actor("Player", 1)
	var enemy := _actor("Enemy", 2)
	log.bind_actors([player, enemy])
	var event := _event(1, 2, "block", 2.5)
	var before := event.duplicate(true)
	log.append_swing(event)
	_check(event == before,
			"printing an event left every key and value alone: %s against %s" % [str(event), str(before)])
	_check(event.size() == before.size(), "and added no key of its own: %d against %d" % [
			event.size(), before.size()])
	var lines := _lines_of(log)
	_check(lines.size() == 1, "one event, one line")
	log.append_swing(event)
	var twice := _lines_of(log)
	_check(twice.size() == 2 and String(twice[0]) == String(twice[1]),
			"the same dictionary a second time says the same thing: %s" % str(twice))
	player.free()
	enemy.free()
	log.free()


# A runtime error inside a case prints an engine error and abandons that function, so "no FAIL
# lines" cannot tell a passing case from one that never finished. Every case is run through this,
# and each has to make a minimum number of checks of its own to count as having run.
func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	await _case("the six sentences", 12, _every_outcome_is_one_true_sentence)
	await _case("a defence worth nothing", 7, _a_defence_worth_nothing_is_still_named)
	await _case("a zero without a defence", 7, _a_zero_without_a_defence_refuses_to_invent_one)
	await _case("names from the scene", 5, _names_come_from_the_scene_and_stay_plain_text)
	await _case("the shared amount", 25, _the_amount_is_the_number_the_float_prints)
	await _case("the last eight lines", 9, _the_chat_keeps_the_last_eight_lines)
	await _case("click-through and bounds", 45,
			_the_panel_never_eats_a_click_and_never_leaves_the_screen)
	await _case("entries wrap and the panel regrows", 6,
			_the_panel_regrows_when_the_entries_wrap)
	await _case("no second drain", 12, _the_chat_holds_no_second_drain_of_its_own)
	await _case("an event comes through untouched", 4, _a_reported_event_comes_through_untouched)

	_check(checks > 170, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Combat log: all checks passed")
	quit(1 if failures else 0)


# Every case above leaves the sheets exactly as it found them: the chat may read names, and may
# not touch anything else.
func _uncheck_actors(player: Node, enemy: Node) -> void:
	_check(_close(float(player.sheet.get_health()), float(player.sheet.get_max_health())),
			"the chat wounded nobody it was told about")
	_check(_close(float(enemy.sheet.get_health()), float(enemy.sheet.get_max_health())),
			"nor the target of a line it printed")
