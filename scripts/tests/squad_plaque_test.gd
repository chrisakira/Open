extends SceneTree

# The squad plaque: the player's visible proof of who is in the squad.
#
# It is a pure view. C++ owns membership, and this control only reads it: every row mirrors the
# sheet's own `is_in_squad()` answer re-read on the frame it is drawn, so a member who joins or
# dies is shown by the next frame and not by anything this control inferred. It decides nothing
# about who the founder is either - the scene that asked C++ to seat somebody says who.
#
# The locked shape (docs/recruiting.md, "The squad plaque"): wood panel, brass frame, the word
# SQUAD above the names, the right screen edge 16 px in and vertically centred, and *always on*:
# the heading stands over zero rows, because an empty squad is still a squad.
#
# The control is reached by loading its path and duck-typing the calls, so a missing script or a
# missing door fails as a counted assertion in this suite rather than a parse cascade that ends
# the run with nothing counted.

var failures := 0
var checks := 0

const PLAQUE_PATH := "res://scripts/squad_list.gd"
const WORLD_PATH := "res://scenes/main.tscn"

const WOOD := Color("#241B16")
const BRASS := Color("#806343")
const HEADING_COLOR := Color("#BD9862")
const NAME_COLOR := Color("#F4E6C6")


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

func _actor(node_name: String, id: int, sheet_name: String = "") -> StandingDummy:
	# A real actor node with a real configured sheet behind it. Unnamed sheets are the
	# fallback case: the plaque reads the sheet name first, the node name underneath it.
	var actor := StandingDummy.new()
	actor.name = node_name
	actor.setup(0, 1, id, id * 7 + 1, sheet_name)
	return actor


func _seated(actor: StandingDummy) -> void:
	# The honest door to membership: the coordinator seats a registered sheet. Nothing else
	# in this suite, or in the game, can put a name on this plaque.
	var combat = OpenCombat.new()
	_check(combat.add_character(actor.sheet, Vector3.ZERO), "the seated sheet registers")
	_check(combat.found_squad(actor.sheet.get_character_id()), "the coordinator seats the sheet")


func _plaque() -> Control:
	var script: Script = load(PLAQUE_PATH)
	if script == null:
		_check(false, "the squad plaque exists at %s" % PLAQUE_PATH)
		return null
	var control: Control = script.new()
	if control == null:
		_check(false, "the squad plaque instantiates")
		return null
	var host := Control.new()
	host.name = "Host"
	root.add_child(host)
	host.add_child(control)
	return control


func _rows_of(control: Control) -> PackedStringArray:
	var found := PackedStringArray()
	for label in control.find_children("*", "Label", true, false):
		if String(label.name).begins_with("Member_"):
			found.append(String(label.text))
	return found


func _heading_of(control: Control) -> Label:
	var found: Array = control.find_children("Heading", "Label", true, false)
	return null if found.is_empty() else found[0] as Label


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


func _find(node: Node, target: String) -> Node:
	if node.name == target:
		return node
	for child in node.get_children():
		var found := _find(child, target)
		if found != null:
			return found
	return null


func _count(node: Node, target: String) -> int:
	var total := 0
	if node.name == target:
		total += 1
	for child in node.get_children():
		total += _count(child, target)
	return total


func _stage(control, viewport_size: Vector2i) -> SubViewport:
	# A viewport the test owns: the root window cannot be resized from a headless script, so
	# "the plaque fits a smaller window" is asked of a SubViewport that really gets smaller.
	# The control moves here rather than being cloned into it: an anchored control reads the
	# rect of whatever viewport really holds it, and the host it came from is not one.
	var view := SubViewport.new()
	view.size = viewport_size
	root.add_child(view)
	var layer := CanvasLayer.new()
	layer.name = "TestHud"
	view.add_child(layer)
	var previous: Node = control.get_parent()
	if previous != null:
		previous.remove_child(control)
	layer.add_child(control)
	await process_frame
	await process_frame
	return view


func _unstage(view: SubViewport, control: Control) -> void:
	# Back to the tree the test keeps, so freeing the staged viewport does not free the control
	# under a case that still means to look at it.
	var previous := control.get_parent()
	if previous != null:
		previous.remove_child(control)
	root.add_child(control)
	view.free()
	await process_frame
	await process_frame


# --- the scene founds its own squad -------------------------------------------

func _the_scene_founds_the_squad_with_the_player() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	_check(player != null and player.sheet != null, "the world has a player with a sheet")
	if player.sheet == null:
		await _free_world(world)
		return
	_check(bool(player.sheet.call("is_in_squad")), "the player starts in the squad it commands")
	_check(not bool(jane.sheet.call("is_in_squad")), "Jane starts outside it")
	_check(not bool(enemy.sheet.call("is_in_squad")), "and so does the enemy")
	_check(int(player.sheet.get_faction()) == 0, "founding never moved the player's faction")
	_check(bool(jane.sheet.call("is_recruitable")), "founding never spent Jane's flag")
	_check(int(jane.sheet.get_faction()) == 0, "nor moved anybody's faction from the bench")
	_check(int(enemy.sheet.get_faction()) == 1, "the enemy is still hostile")
	await _free_world(world)


func _the_plaque_is_on_the_hud_over_the_founder() -> void:
	var world = await _world()
	var plaque := _find(world, "SquadList")
	_check(plaque != null, "the scene carries a SquadList")
	if plaque == null:
		await _free_world(world)
		return
	_check(_count(world, "SquadList") == 1, "and exactly one plaque per scene")
	_check(plaque.get_parent() is CanvasLayer, "the plaque lives on the HUD canvas layer")
	_check(bool(plaque.visible), "and it is on from the first frame")
	var rows := _rows_of(plaque)
	_check(rows.size() == 1, "a squad of the founder alone shows one row, got %s" % str(rows))
	if rows.size() == 1:
		_check(String(rows[0]) == "John Doe", "the row is the player's sheet name: got [%s]"
				% String(rows[0]))
	var heading := _heading_of(plaque)
	_check(heading != null and bool(heading.visible), "and the SQUAD heading stands above it")
	if heading != null:
		_check(String(heading.text).to_upper() == "SQUAD", "the heading says SQUAD: got [%s]"
				% String(heading.text))
	_check(plaque.find_children("*", "Button", true, false).is_empty()
			&& plaque.find_children("*", "LineEdit", true, false).is_empty(),
			"and nothing on it can be clicked into an order")
	# The locked shape, in the locked colours: docs/recruiting.md names four hexes and a serif,
	# and a plaque that quietly drifts to the popup palette would still pass every test above.
	_check(_close_colors(_color_of(plaque, "WOOD"), WOOD), "the field is the main-panel wood")
	_check(_close_colors(_color_of(plaque, "BRASS"), BRASS), "the frame is the muted brass")
	_check(_close_colors(_color_of(plaque, "HEADING_COLOR"), HEADING_COLOR),
			"the heading is the muted gold of a group heading")
	_check(_close_colors(_color_of(plaque, "NAME_COLOR"), NAME_COLOR),
			"the names are the light parchment of a title")
	if heading != null:
		_check(_close_colors(_font_color(heading), HEADING_COLOR),
				"and the heading label actually wears that colour")
		var row: Label = plaque.find_children("Member_*", "Label", true, false)[0]
		_check(_close_colors(_font_color(row), NAME_COLOR),
				"and a name label wears the parchment one")
		var serif: SystemFont = row.get("theme_override_fonts/font")
		_check(serif != null and String(serif.font_names[0]) == "Georgia"
				&& serif.font_names[serif.font_names.size() - 1] == "serif",
				"the names are set in the project's serif stack, not the theme default")
	# And it draws under the popups, the way every other view does: a later sibling is the one
	# on top, so the plaque must be an earlier one.
	var layer: Node = plaque.get_parent()
	var above := 0
	var seen := 0
	for popup_name in ["CharacterPopup", "EquipmentPopup", "TechniquePopup", "WorkbenchPopup",
			"AttackPopup", "WeaponPopup"]:
		var popup := _find(world, popup_name)
		if popup != null:
			seen += 1
			if popup.get_parent() == layer and popup.get_index() > plaque.get_index():
				above += 1
	_check(seen == 6 and above == 6,
			"every popup still draws over the squad list (saw %d, above %d)" % [seen, above])
	await _free_world(world)


# --- the plaque itself ---------------------------------------------------------

func _it_is_always_on_even_over_an_empty_squad() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var heading := _heading_of(plaque)
	_check(heading != null, "the plaque grew its heading")
	_check(bool(heading.visible), "the heading is visible over no rows at all")
	_check(_rows_of(plaque).is_empty(), "and an empty squad shows no name")
	_check(bool(plaque.visible), "and the panel itself never hides itself")
	for control in [plaque] + plaque.find_children("*", "Control", true, false):
		_check(int(control.mouse_filter) == int(Control.MOUSE_FILTER_IGNORE),
				"%s never eats a click" % String(control.name))
	await process_frame
	plaque.queue_free()
	await process_frame


func _the_founder_sits_first_then_the_names_by_id() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var second := _actor("Second", 2, "Second")
	var ninth := _actor("Ninth", 9, "Ninth")
	var founder := _actor("Founder", 5, "Founder")
	_seated(founder)
	_seated(ninth)
	_seated(second)
	var an_outsider := _actor("Outsider", 1, "Outsider")
	_check(not bool(an_outsider.sheet.call("is_in_squad")), "the outsider is not seated")
	plaque.call("bind_squad", [founder, ninth, second, an_outsider], founder)
	plaque.refresh()
	var rows := _rows_of(plaque)
	_check(rows.size() == 3, "the outsider gets no row, the seated three do: got %s" % str(rows))
	if rows.size() == 3:
		_check(String(rows[0]) == "Founder", "the founder keeps the first seat: got [%s]"
				% String(rows[0]))
		_check(String(rows[1]) == "Second" and String(rows[2]) == "Ninth",
				"and the rest follow in character-id order, not the order they were bound: got %s"
						% str(rows))
	plaque.queue_free()
	second.queue_free()
	ninth.queue_free()
	founder.queue_free()
	an_outsider.queue_free()
	await process_frame


func _a_joined_name_appears_and_a_dead_one_goes() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var founder := _actor("Founder", 5, "Founder")
	var other := _actor("Other", 2, "Other")
	_seated(founder)
	_check(not bool(other.sheet.call("is_in_squad")), "the second sheet starts outside")
	plaque.call("bind_squad", [founder, other], founder)
	await process_frame
	var rows := _rows_of(plaque)
	_check(rows.size() == 1, "the first drawn frame shows the one member: got %s" % str(rows))
	# Joining is somebody else's door; the plaque only re-reads. Nobody calls refresh here.
	_seated(other)
	await process_frame
	rows = _rows_of(plaque)
	_check(rows.size() == 2, "a seat minted after the fact shows without a manual refresh: %s"
			% str(rows))
	if rows.size() == 2:
		_check(String(rows[0]) == "Founder" and String(rows[1]) == "Other",
				"the newcomer follows the founder in id order: got %s" % str(rows))
	# Death is the one way out, and the sheet's own answer is what removes the row.
	other.sheet.apply_damage(1, 500.0)
	await process_frame
	await process_frame
	rows = _rows_of(plaque)
	_check(rows.size() == 1, "a dead member leaves the plaque: got %s" % str(rows))
	if rows.size() == 1:
		_check(String(rows[0]) == "Founder", "and the one who remains is the founder: got [%s]"
				% String(rows[0]))
	plaque.queue_free()
	founder.queue_free()
	other.queue_free()
	await process_frame


func _the_names_come_from_the_sheet_and_stop_at_twenty() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var named := _actor("Nodey", 2, "Named In The Sheet")
	var unnamed := _actor("Unnamed", 3, "")
	var long_one := _actor("Longnode", 4, "A Very Unreasonably Long Character Name")
	_seated(named)
	_seated(unnamed)
	_seated(long_one)
	plaque.call("bind_squad", [named, unnamed, long_one], named)
	plaque.refresh()
	var rows := _rows_of(plaque)
	_check(rows.size() == 3, "three seated sheets, three rows: got %s" % str(rows))
	if rows.size() == 3:
		# Founder first (id 2), then id 3, then id 4.
		_check(String(rows[0]) == "Named In The Sheet",
				"a named sheet is named, not the node under it: got [%s]" % String(rows[0]))
		_check(String(rows[1]) == "Unnamed",
				"a sheet with no name falls back to the node name: got [%s]" % String(rows[1]))
		_check(String(rows[2]) == "A Very Unreasonably…",
				"a name past twenty is cut exactly as the plates cut it: got [%s]"
						% String(rows[2]))
	_check(not String(rows[0]).contains("_"), "the rows are plain names, not markup: got %s"
			% str(rows))
	plaque.queue_free()
	named.queue_free()
	unnamed.queue_free()
	long_one.queue_free()
	await process_frame


func _the_plaque_stays_inside_the_screen_on_the_right_edge() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var founder := _actor("Founder", 5, "Founder")
	_seated(founder)
	var seats: Array = [founder]
	for id in range(6, 14):
		var member := _actor("Member %d" % id, id, "Member %d" % id)
		_seated(member)
		seats.append(member)
	plaque.call("bind_squad", seats, founder)
	var view = await _stage(plaque, Vector2i(1280, 720))
	for size in [Vector2i(1280, 720), Vector2i(720, 420), Vector2i(1024, 600)]:
		view.size = size
		await process_frame
		await process_frame
		var rect: Rect2 = plaque.get_global_rect()
		var screen := Rect2(Vector2.ZERO, Vector2(view.get_visible_rect().size))
		_check(rect.size.y > 24.0 and rect.size.x > 24.0, "the plaque has a real shape %s: %s"
				% [str(size), str(rect.size)])
		_check(rect.position.x >= -0.5 and rect.end.x <= screen.size.x + 0.5,
				"it never leaves the screen sideways %s: %s in %s" % [str(size), str(rect),
						str(screen)])
		_check(rect.position.y >= -0.5 and rect.end.y <= screen.size.y + 0.5,
				"nor vertically %s: %s in %s" % [str(size), str(rect), str(screen)])
		_check(_close(rect.end.x, screen.size.x - 16.0, 1.0),
				"its right edge sits 16 px in %s: got %s against %f" % [str(size), rect.end.x,
						screen.size.x - 16.0])
		_check(_close(rect.get_center().y, screen.size.y / 2.0, 1.0),
				"and it is vertically centred %s: got %f against %f" % [str(size),
						rect.get_center().y, screen.size.y / 2.0])
		_check(rect.size.y <= screen.size.y * 0.9,
				"and twelve names never outgrow the window %s: %s" % [str(size), str(rect.size)])
	_check(_rows_of(plaque).size() == 9, "the founder plus eight members fill nine rows: got %s"
			% str(_rows_of(plaque)))
	await _unstage(view, plaque)
	plaque.queue_free()
	for member in seats:
		member.queue_free()
	await process_frame


func _the_plaque_writes_nothing_it_was_told_to_read() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var founder := _actor("Founder", 5, "Founder")
	var other := _actor("Other", 2, "Other")
	_seated(other)
	var before := {
		"founder_squad": bool(founder.sheet.call("is_in_squad")),
		"founder_flag": bool(founder.sheet.call("is_recruitable")),
		"founder_faction": int(founder.sheet.get_faction()),
		"founder_target": int(founder.sheet.call("get_recruit_target_id")),
		"other_squad": bool(other.sheet.call("is_in_squad")),
		"other_flag": bool(other.sheet.call("is_recruitable")),
		"other_faction": int(other.sheet.get_faction()),
		"other_condition": int(other.sheet.call("get_recruit_condition")),
	}
	plaque.call("bind_squad", [founder, other], founder)
	plaque.refresh()
	plaque.refresh()
	await process_frame
	_check(bool(founder.sheet.call("is_in_squad")) == bool(before["founder_squad"]),
			"reading a squad seated nobody")
	_check(bool(founder.sheet.call("is_recruitable")) == bool(before["founder_flag"]),
			"nor spent a flag on the way past")
	_check(int(founder.sheet.get_faction()) == int(before["founder_faction"]),
			"nor moved a faction")
	_check(int(founder.sheet.call("get_recruit_target_id")) == int(before["founder_target"]),
			"nor ordered anybody to ask")
	_check(bool(other.sheet.call("is_in_squad")) == bool(before["other_squad"]),
			"nor unseated the member it printed")
	_check(bool(other.sheet.call("is_recruitable")) == bool(before["other_flag"]),
			"nor spent the member's flag")
	_check(int(other.sheet.get_faction()) == int(before["other_faction"]),
			"nor re-factioned a recruit")
	_check(int(other.sheet.call("get_recruit_condition")) == int(before["other_condition"]),
			"nor rewrote a condition")
	plaque.queue_free()
	founder.queue_free()
	other.queue_free()
	await process_frame


func _it_folows_the_squad_across_a_rebind() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var founder := _actor("Founder", 5, "Founder")
	_seated(founder)
	plaque.call("bind_squad", [founder], founder)
	plaque.refresh()
	_check(_rows_of(plaque).size() == 1, "the first squad shows its one name")
	var second_world := _actor("Newcomer", 6, "A Fresh Face")
	_seated(second_world)
	# A rebind replaces the whole bound squad, exactly the way the overlay replaces its scene.
	plaque.call("bind_squad", [second_world], second_world)
	plaque.refresh()
	var rows := _rows_of(plaque)
	_check(rows.size() == 1, "a rebound roster shows its one row: got %s" % str(rows))
	if rows.size() == 1:
		_check(String(rows[0]) == "A Fresh Face", "the old member is gone with the old roster: [%s]"
				% String(rows[0]))
	plaque.queue_free()
	founder.queue_free()
	second_world.queue_free()
	await process_frame


# --- the run ------------------------------------------------------------------

func _color_of(control, constant_name: String) -> Color:
	var table: Dictionary = control.get_script().get_script_constant_map()
	if not table.has(constant_name):
		_check(false, "the plaque states a %s constant for the locked palette" % constant_name)
		return Color(9.0, 9.0, 9.0)  # not a colour any palette uses
	return table[constant_name] as Color


func _font_color(label: Label) -> Color:
	if not label.has_theme_color_override("font_color"):
		_check(false, "%s sets no font colour override" % String(label.name))
		return Color(9.0, 9.0, 9.0)
	return label.get("theme_override_colors/font_color")


func _close_colors(left: Color, right: Color) -> bool:
	return _close(left.r, right.r) and _close(left.g, right.g) and _close(left.b, right.b)


func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	await _case("the scene founds its squad", 8, _the_scene_founds_the_squad_with_the_player)
	await _case("the HUD plaque over the founder", 17, _the_plaque_is_on_the_hud_over_the_founder)
	await _case("always on over no rows", 6, _it_is_always_on_even_over_an_empty_squad)
	await _case("founder first, then by id", 6, _the_founder_sits_first_then_the_names_by_id)
	await _case("joins and deaths need no refresh call", 7,
			_a_joined_name_appears_and_a_dead_one_goes)
	await _case("names from the sheet", 7, _the_names_come_from_the_sheet_and_stop_at_twenty)
	await _case("the right edge at three sizes", 18,
			_the_plaque_stays_inside_the_screen_on_the_right_edge)
	await _case("no write of anybody's state", 8,
			_the_plaque_writes_nothing_it_was_told_to_read)
	await _case("a rebind replaces the roster", 3, _it_folows_the_squad_across_a_rebind)

	_check(checks > 68, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Squad plaque: all checks passed")
	quit(1 if failures else 0)
