extends SceneTree

# Task 14a: the technique popup, driven through its real controls.
#
# The panel is presentation, so the checks are about what a click does to the sheet and what
# the sheet says back. Two data sources are used on purpose:
#
#   * the player's own sheet, for everything a bare-handed character can already do - the
#     sequence it holds, the switch, and the fact that hands offer no technique at all;
#   * a test-only recorder that answers with the declared snapshot schema, for the rows no
#     script can produce in 14a: techniques a held weapon offers, one locked behind a rank,
#     and a clock still coming down. The recorder is a fake *answer* and never a fake rule:
#     the panel still only draws what it is told and sends one command per click.
#
# What this is picky about: opening a panel is not a command; a command is exactly one
# `configure_techniques`; a technique the weapon does not offer is shown and kept rather than
# hidden or deleted; and no click on this panel can move, attack or equip.

const TECHNIQUE_A := 1
const TECHNIQUE_B := 2
const TECHNIQUE_LOCKED := 3
const TECHNIQUE_FOREIGN := 900

var failures := 0
var _viewport_size := Vector2.ZERO


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _click_at(point: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	root.push_input(motion, true)
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.position = point
		event.global_position = point
		event.pressed = pressed
		root.push_input(event, true)


func _key(keycode: int) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = keycode
		event.physical_keycode = keycode
		event.pressed = pressed
		root.push_input(event, true)


func _texts(node: Node, out: Array) -> void:
	for child in node.get_children():
		if child is Label:
			out.append(String((child as Label).text))
		elif child is Button:
			out.append(String((child as Button).text))
		_texts(child, out)


func _any_text(popup: Node, needle: String) -> bool:
	var texts := []
	_texts(popup, texts)
	return texts.any(func(text: String) -> bool: return needle in text)


func _button(popup: Node, button_name: String) -> Button:
	var found := popup.find_children(button_name, "Button", true, false)
	return found[0] as Button if found.size() == 1 else null


func _has_button(popup: Node, button_name: String) -> bool:
	return popup.find_children(button_name, "Button", true, false).size() == 1


func _described(snapshot: Dictionary) -> String:
	return "%s|%s|%s|%s" % [str(snapshot.get("enabled")), str(snapshot.get("index")),
		str(snapshot.get("sequence")), str(snapshot.get("cooldowns"))]


func _ids(sheet) -> Array:
	var ids := []
	for row in sheet.get_technique_snapshot().get("sequence", []):
		ids.append(int(row["technique_id"]))
	return ids


# The picker must reach a character before the floor behind it, so a floor probe has to name
# the collider a real click would hit first.
func _nearest_is_floor(world: Node, camera: Camera3D, screen: Vector2) -> bool:
	var from := camera.project_ray_origin(screen)
	var to := from + camera.project_ray_normal(screen) * 1000.0
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.collision_mask = 1 | 2
	query.collide_with_areas = false
	var hit: Dictionary = world.get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return false
	var body: Node = hit.get("collider", null)
	return body != null and not (body is Player or body is StandingDummy)


# A screen point that really lands on world geometry, outside any open popup.
func _floor_screen(world: Node, camera: Camera3D, anchor: Vector3, popup) -> Vector2:
	for offset in [Vector3(0.0, 0.0, -4.0), Vector3(-5.0, 0.0, 0.0), Vector3(0.0, 0.0, 7.0),
			Vector3(-7.0, 0.0, 3.0), Vector3(7.0, 0.0, 3.0), Vector3(0.0, 0.0, -9.0),
			Vector3(-12.0, 0.0, -6.0), Vector3(12.0, 0.0, -6.0)]:
		var screen: Vector2 = camera.unproject_position(anchor + offset)
		if screen.x < 8.0 or screen.y < 8.0 or screen.x > _viewport_size.x - 8.0 \
				or screen.y > _viewport_size.y - 8.0:
			continue
		if popup != null and popup.visible and popup.get_global_rect().has_point(screen):
			continue
		if _nearest_is_floor(world, camera, screen):
			return screen
	return Vector2.INF


# --- the test-only snapshot source --------------------------------------------

# One row of the declared technique schema. A popup row is drawn entirely out of this, so a
# test that can name these keys can name what a panel must show.
func _technique_row(technique_id: int) -> Dictionary:
	return {
		"technique_id": technique_id,
		"defined": true,
		"available": true,
		"skip_on_cooldown": false,
		"required_rank": 1,
		"trained": 12,
		"cut": 1.25,
		"pierce": 1.0,
		"blunt": 1.0,
		"cooldown": 6.0,
		"has_stamina_cost": false,
		"stamina_cost": 0.0,
		"cooldown_remaining": 0.0,
	}


class Recorder:
	extends RefCounted

	var configured := true
	var can_configure := true
	var player_controlled := true
	var index := 0
	var enabled := true
	var supported: Array = []
	var cooldowns: Array = []
	var steps: Array = []
	var commands: Array = []

	func is_configured() -> bool:
		return configured

	func get_technique_snapshot() -> Dictionary:
		var sequence := []
		for step in steps:
			var row := _row_for(int(step["technique_id"]))
			row["skip_on_cooldown"] = bool(step["skip_on_cooldown"])
			sequence.append(row)
		return {
			"enabled": enabled,
			"player_controlled": player_controlled,
			"can_configure": can_configure,
			"index": index,
			"supported": supported,
			"sequence": sequence,
			"cooldowns": cooldowns,
		}

	func configure_techniques(next_enabled: bool, sent: Array) -> bool:
		commands.append({"enabled": next_enabled, "steps": sent.duplicate(true)})
		enabled = next_enabled
		steps = sent.duplicate(true)
		return true

	# A sequence entry the supported list does not contain is an entry for a technique this
	# weapon does not offer: kept, unresolved, and unavailable.
	func _row_for(technique_id: int) -> Dictionary:
		for row in supported:
			if int(row["technique_id"]) == technique_id:
				return row.duplicate(true)
		return {
			"technique_id": technique_id,
			"defined": false,
			"available": false,
			"skip_on_cooldown": false,
			"required_rank": 0,
			"trained": 0,
			"cut": 1.0,
			"pierce": 1.0,
			"blunt": 1.0,
			"cooldown": 0.0,
			"has_stamina_cost": false,
			"stamina_cost": 0.0,
			"cooldown_remaining": 0.0,
		}


func _recorder() -> Recorder:
	var recorder := Recorder.new()
	var cooling := _technique_row(TECHNIQUE_B)
	cooling["cooldown_remaining"] = 3.2
	var locked := _technique_row(TECHNIQUE_LOCKED)
	locked["available"] = false
	# A technique this sheet has simply not earned yet: the definition is real, and the panel
	# has to say which of the two it is looking at.
	locked["defined"] = true
	locked["required_rank"] = 50
	recorder.supported = [_technique_row(TECHNIQUE_A), cooling, locked]
	recorder.cooldowns = [cooling]
	recorder.steps = [{"technique_id": TECHNIQUE_A, "skip_on_cooldown": false},
		{"technique_id": TECHNIQUE_FOREIGN, "skip_on_cooldown": true}]
	return recorder


# --- the world and its popups -------------------------------------------------

func _world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	return world


func _popup_of(world: Node):
	var found := world.find_children("TechniquePopup", "", true, false)
	_check(found.size() == 1, "the world has exactly one technique popup")
	return found[0] if found.size() == 1 else null


func _free(world: Node) -> void:
	world.queue_free()
	await process_frame


# --- the popup over real starter weapons (Task 14b) ----------------------------

const SLOT_RIGHT_HAND := 6
const FOREIGN_ENTRY := 4242


func _real_id_of_model(sheet, model_key: String) -> int:
	for row in sheet.get_equipment_snapshot():
		if String(row["model_key"]) == model_key:
			return int(row["item_id"])
	return 0


func _supported_ids(sheet) -> Array:
	var ids := []
	for row in sheet.get_technique_snapshot().get("supported", []):
		ids.append(int(row["technique_id"]))
	return ids


func _a_real_sword_offers_real_moves() -> void:
	var world = await _world()
	var popup = _popup_of(world)
	var player: Node = world.get_node("Player")
	if popup == null:
		await _free(world)
		return
	var sheet = player.sheet

	# The world's own integration: the player's sheet is the one a human drives, so it is the
	# one the scene hands over. The dummies stay simulation-driven, and no popup click flips
	# this - opening a panel is not a handover.
	_check(bool(sheet.get_technique_snapshot().get("player_controlled")),
			"the world made the player's own sheet player-controlled")
	_check(not bool(world.get_node("Enemy").get("sheet").get_technique_snapshot()
			.get("player_controlled")),
			"while the dummy is still chosen for by the simulation")

	# Equipping the real sword is what makes the real moves exist. The click path that moves
	# the item is the Equipment popup's own suite; here the same bridge command.
	var sword := _real_id_of_model(sheet, "Sword")
	_check(sheet.equip_item(sword, SLOT_RIGHT_HAND), "the sword the player owns goes into hand")
	_check(_supported_ids(sheet) == [TECHNIQUE_A, TECHNIQUE_B],
			"and the sheet now really supports Slash and Thrust")

	# Opening the panel must not be able to touch anything a swing spends.
	var before := _described(sheet.get_technique_snapshot())
	var stamina_before: float = sheet.get_stamina()
	popup.open_for_sheet(sheet, Vector2(30.0, 30.0))
	_check(_described(sheet.get_technique_snapshot()) == before,
			"opening sent no command, drew no technique and reset no index")
	_check(_close(sheet.get_stamina(), stamina_before, 0.0001),
			"and the sheet paid nothing for the panel appearing")
	await process_frame
	await process_frame
	_check(_any_text(popup, "Techniques: On"), "the real panel opens reading the real sheet")
	var add_slash := _button(popup, "Add%d" % TECHNIQUE_A)
	var add_thrust := _button(popup, "Add%d" % TECHNIQUE_B)
	_check(add_slash != null and not add_slash.disabled
			and add_thrust != null and not add_thrust.disabled,
			"both moves the sword really offers can be added")
	_check(not _has_button(popup, "Add%d" % 3),
			"and the hammer's move is not offered to a sword at all")

	# Add, duplicate and Skip, all through the controls, all read back from the sheet.
	_click_at(add_slash.get_global_rect().get_center())
	await process_frame
	_click_at(add_slash.get_global_rect().get_center())
	await process_frame
	_click_at(add_thrust.get_global_rect().get_center())
	await process_frame
	_check(_ids(sheet) == [TECHNIQUE_A, TECHNIQUE_A, TECHNIQUE_B],
			"two Slashes and a Thrust, added as duplicates through the controls")
	_click_at(_button(popup, "Skip1").get_global_rect().get_center())
	await process_frame
	var saved: Array = sheet.get_technique_snapshot().get("sequence", [])
	_check(saved.size() == 3 and bool(saved[1]["skip_on_cooldown"])
			and not bool(saved[0]["skip_on_cooldown"])
			and not bool(saved[2]["skip_on_cooldown"]),
			"Skip belongs to the entry it was pressed on")

	# The switch, saved.
	_click_at(_button(popup, "EnableButton").get_global_rect().get_center())
	await process_frame
	_check(not bool(sheet.get_technique_snapshot().get("enabled")),
			"the header button really switches the configured state off")
	_click_at(_button(popup, "EnableButton").get_global_rect().get_center())
	await process_frame

	# A saved entry the current weapon does not offer is kept across real swaps, and support
	# follows the held item. The entry below is an id no weapon offers; the hammer's move is
	# offered by exactly one.
	_check(sheet.configure_techniques(true,
			[_row_step(TECHNIQUE_A, false), _row_step(FOREIGN_ENTRY, true)]),
			"the sheet holds a Slash and an entry nothing offers")
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "the sword goes back")
	_check(_supported_ids(sheet).is_empty(), "bare hands offer nothing through a real sheet")
	_check(_ids(sheet) == [TECHNIQUE_A, FOREIGN_ENTRY], "and the sequence kept both entries")
	_check(sheet.equip_item(_real_id_of_model(sheet, "Hammer_Small"), SLOT_RIGHT_HAND),
			"the hammer goes up")
	_check(_supported_ids(sheet) == [3], "and the hammer offers exactly its own move")
	await process_frame
	_check(_has_button(popup, "Add3"), "the panel follows the weapon without being reopened")
	_check(_any_text(popup, "not offered now"),
			"while the configured Slash and the foreign entry stay listed as unavailable")
	_check(_ids(sheet) == [TECHNIQUE_A, FOREIGN_ENTRY], "and swapping deleted nothing")
	await _free(world)


func _row_step(technique_id: int, skip: bool) -> Dictionary:
	return {"technique_id": technique_id, "skip_on_cooldown": skip}


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _run() -> void:
	_viewport_size = root.get_visible_rect().size
	await _entry_and_live_sheet()
	await _recorder_rows_and_commands()
	await _input_isolation()
	await _edges()
	await _a_real_sword_offers_real_moves()
	if failures == 0:
		print("Technique popup: all checks passed")
	quit(1 if failures else 0)


# --- opening from the sheet, on the player's real sheet -----------------------

func _entry_and_live_sheet() -> void:
	var world = await _world()
	var popup = _popup_of(world)
	var player: Node = world.get_node("Player")
	if popup == null:
		await _free(world)
		return
	for method in ["bind_sheet", "open_for_sheet", "render_snapshot", "covers", "close"]:
		_check(popup.has_method(method), "the popup exposes %s" % method)
	_check(not popup.visible, "the popup starts hidden")

	# The entry lives on the character sheet, and the sheet is the only thing that has it.
	_key(KEY_I)
	await process_frame
	var entries := world.find_children("TechniquesButton", "Button", true, false)
	_check(entries.size() == 1, "the character sheet carries a Techniques entry")
	if entries.size() != 1:
		await _free(world)
		return

	# A sequence the sheet really holds, so that "opening changed nothing" means something.
	var steps = [{"technique_id": TECHNIQUE_A, "skip_on_cooldown": false},
		{"technique_id": TECHNIQUE_B, "skip_on_cooldown": false},
		{"technique_id": TECHNIQUE_FOREIGN, "skip_on_cooldown": false}]
	_check(player.sheet.configure_techniques(true, steps), "the sheet takes a sequence")
	var before := _described(player.sheet.get_technique_snapshot())

	_click_at((entries[0] as Button).get_global_rect().get_center())
	await process_frame
	await process_frame
	_check(popup.visible, "the sheet's Techniques entry opens the panel")
	_check(_described(player.sheet.get_technique_snapshot()) == before,
			"opening the panel issued no command and reset no state")

	# The panel is its own window: the sheet it was opened from is not its owner, and closing
	# that leaves this one reading the same sheet.
	_key(KEY_I)
	await process_frame
	_check(popup.visible, "the panel stays when the sheet it came from closes")

	# What the bare-handed sheet really is: enabled, player-driven since the scene handed it
	# over, nothing offered - a sequence names no move until a weapon does - and three
	# configured entries nothing supports: kept, and saying so.
	_check(_any_text(popup, "Techniques: On"), "the panel shows the switch as the sheet holds it")
	_check(_button(popup, "Add%d" % TECHNIQUE_A) == null,
			"with bare hands there is nothing to add, and no invented technique appears")
	_check(_any_text(popup, "not offered now"),
			"an entry the held item does not offer is shown as unavailable")
	_check(_has_button(popup, "Skip2") and _has_button(popup, "Remove2"),
			"all three configured entries are drawn, each with its own controls")

	# The panel follows the sheet: an edit made elsewhere is on screen a frame later, without
	# the panel being reopened.
	_check(player.sheet.configure_techniques(false, steps), "the sheet is switched off")
	await process_frame
	await process_frame
	_check(_any_text(popup, "Techniques: Off"), "and the open panel says so by itself")
	_check(player.sheet.configure_techniques(true, steps), "and on again")
	await process_frame

	# A real command from a real click: Skip on the middle entry, which is that entry's own
	# setting and nobody else's, and which leaves the switch alone.
	_click_at(_button(popup, "Skip1").get_global_rect().get_center())
	await process_frame
	var saved: Array = player.sheet.get_technique_snapshot().get("sequence", [])
	_check(saved.size() == 3 and bool(saved[1]["skip_on_cooldown"])
			and not bool(saved[0]["skip_on_cooldown"])
			and not bool(saved[2]["skip_on_cooldown"]),
			"a row's Skip button sets that entry and only that entry")
	_check(bool(player.sheet.get_technique_snapshot().get("enabled")),
			"and a step edit carries the switch through as it was, rather than resetting it")

	# Up, Down, Duplicate and Remove, each read back from the sheet.
	_click_at(_button(popup, "Up2").get_global_rect().get_center())
	await process_frame
	_check(_ids(player.sheet) == [TECHNIQUE_A, TECHNIQUE_FOREIGN, TECHNIQUE_B],
			"Up moves the entry up one place")
	_click_at(_button(popup, "Down0").get_global_rect().get_center())
	await process_frame
	_check(_ids(player.sheet) == [TECHNIQUE_FOREIGN, TECHNIQUE_A, TECHNIQUE_B],
			"Down moves it back down")
	_click_at(_button(popup, "Copy1").get_global_rect().get_center())
	await process_frame
	_check(_ids(player.sheet) == [TECHNIQUE_FOREIGN, TECHNIQUE_A, TECHNIQUE_A, TECHNIQUE_B],
			"Duplicate inserts a copy beside the entry it was pressed on")
	_click_at(_button(popup, "Remove1").get_global_rect().get_center())
	await process_frame
	_check(_ids(player.sheet) == [TECHNIQUE_FOREIGN, TECHNIQUE_A, TECHNIQUE_B],
			"Remove takes exactly one entry away")

	# The switch, from its own button.
	_click_at(_button(popup, "EnableButton").get_global_rect().get_center())
	await process_frame
	_check(not bool(player.sheet.get_technique_snapshot().get("enabled")),
			"the header button switches techniques off on the sheet")
	_click_at(_button(popup, "EnableButton").get_global_rect().get_center())
	await process_frame
	_check(bool(player.sheet.get_technique_snapshot().get("enabled")), "and on again")

	# Escape, and the panel's own close button.
	_key(KEY_ESCAPE)
	await process_frame
	_check(not popup.visible, "Escape closes the panel")
	player.sheet.configure_techniques(true, steps)
	_key(KEY_I)
	await process_frame
	_click_at((entries[0] as Button).get_global_rect().get_center())
	await process_frame
	_check(popup.visible, "the entry opens it again")
	_click_at(_button(popup, "CloseButton").get_global_rect().get_center())
	await process_frame
	_check(not popup.visible, "and the close button closes it")
	_check(_ids(player.sheet) == [TECHNIQUE_A, TECHNIQUE_B, TECHNIQUE_FOREIGN],
			"and closing sends no command: the sequence is what it was")
	await _free(world)


# --- rows that only a supporting weapon could produce -------------------------

func _recorder_rows_and_commands() -> void:
	var world = await _world()
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return
	var recorder := _recorder()
	popup.open_for_sheet(recorder, Vector2(20.0, 20.0))
	await process_frame
	await process_frame
	_check(recorder.commands.is_empty(),
			"binding a sheet and opening draws what it says, and sends nothing")

	_check(_any_text(popup, "Rank 50 (this swing 12)"),
			"a technique behind a rank is offered as locked, with both numbers from the sheet")
	_check(_any_text(popup, "3.2s"), "and a running cooldown is shown while it runs")
	_check(_any_text(popup, "not offered now"),
			"an entry from another weapon is still listed, marked as unavailable")
	_check(_any_text(popup, "▸ 1."), "the panel marks the entry the sequence is up to")

	var add_a := _button(popup, "Add%d" % TECHNIQUE_A)
	var add_locked := _button(popup, "Add%d" % TECHNIQUE_LOCKED)
	_check(add_a != null and not add_a.disabled, "an unlocked technique can be added")
	_check(add_locked != null and add_locked.disabled,
			"a locked one cannot: the panel will not offer a move the swing would refuse")
	_check(not _has_button(popup, "Add%d" % TECHNIQUE_FOREIGN),
			"and a technique no held weapon offers is not offered as a new addition either")

	_click_at(add_a.get_global_rect().get_center())
	await process_frame
	_check(recorder.commands.size() == 1, "one click sends exactly one command")
	var sent: Dictionary = recorder.commands[0]
	_check(bool(sent["enabled"]), "with the switch left as it was")
	_check(_step_ids(sent["steps"]) == [TECHNIQUE_A, TECHNIQUE_FOREIGN, TECHNIQUE_A],
			"Add appends to the sequence the sheet holds, keeping what was there")

	# The countdown on the panel is the clock the sheet reports, not a timer of its own: the
	# number changes when the snapshot does.
	for row in recorder.supported:
		if int(row["technique_id"]) == TECHNIQUE_B:
			row["cooldown_remaining"] = 1.0
	await process_frame
	await process_frame
	_check(_any_text(popup, "1.0s"), "the refreshed clock on the panel is the one C++ reports")

	# A sheet that cannot be configured has no live controls, and a dead control is not a way
	# to send a command the simulation would refuse.
	recorder.can_configure = false
	await process_frame
	await process_frame
	var enable := _button(popup, "EnableButton")
	_check(enable != null and enable.disabled, "the switch is disabled")
	_check(_button(popup, "Add%d" % TECHNIQUE_A).disabled, "the additions are disabled")
	_check(_button(popup, "Remove0").disabled and _button(popup, "Up0").disabled
			and _button(popup, "Skip0").disabled, "and so is every row control")
	var commands_so_far := recorder.commands.size()
	_click_at(popup.get_global_rect().get_center())
	await process_frame
	_check(recorder.commands.size() == commands_so_far, "and nothing is sent behind them")
	await _free(world)


func _step_ids(steps: Array) -> Array:
	var ids := []
	for step in steps:
		ids.append(int(step["technique_id"]))
	return ids


# --- no click on this panel is an order --------------------------------------

func _input_isolation() -> void:
	var world = await _world()
	var popup = _popup_of(world)
	var player: Node = world.get_node("Player")
	var marker: Node = world.get("_marker")
	var camera: Camera3D = world.get("_camera")
	if popup == null:
		await _free(world)
		return
	var sheet = player.sheet
	_check(sheet.configure_techniques(true, [{"technique_id": TECHNIQUE_A,
			"skip_on_cooldown": false}]), "the sheet holds one entry")
	popup.open_for_sheet(sheet, Vector2(40.0, 40.0))
	await process_frame
	await process_frame

	# A click on the panel's own background, between the controls.
	var inside: Vector2 = popup.get_global_rect().position + Vector2(14.0, 14.0)
	_click_at(inside)
	await process_frame
	_check(popup.visible, "a click inside the panel keeps it open")
	_check(not marker.visible, "and orders no move")
	_check(sheet.velocity_at(player.global_position) == Vector3.ZERO,
			"the player is not told to walk")
	_check(sheet.get_attack_target_id() == 0, "and no attack is ordered")
	_check(_ids(sheet) == [TECHNIQUE_A], "and the sequence is untouched")

	# The other popups keep their own clicks: an enemy is still attackable with this panel up,
	# and the world click that opened its popup closes this one.
	var enemy: Node = world.get_node("Enemy")
	var enemy_point := camera.unproject_position(enemy.global_position + Vector3(0.0, 1.2, 0.0))
	_check(not popup.covers(enemy_point), "the enemy is not hidden behind the panel")
	_click_at(enemy_point)
	await process_frame
	var attack_popups := world.find_children("AttackPopup", "", true, false)
	_check(attack_popups.size() == 1 and attack_popups[0].visible,
			"clicking the enemy still opens its action popup with this panel open")
	_check(not popup.visible, "and the world click closes the technique panel as it closes them")
	_check(sheet.get_attack_target_id() == 0, "with no attack ordered by any of it")

	# A click on the floor beside everything: the normal move still happens, and the panel
	# standing open does not swallow the world.
	popup.open_for_sheet(sheet, Vector2(40.0, 40.0))
	await process_frame
	var floor_point := _floor_screen(world, camera, player.global_position, popup)
	_check(floor_point != Vector2.INF, "a floor point outside the panel is still clickable")
	marker.visible = false
	_click_at(floor_point)
	await process_frame
	_check(not popup.visible, "a world click outside the panel closes it")
	_check(marker.visible, "and the same click goes on to order the move")
	await _free(world)


# --- viewport edges and the schema on its own --------------------------------

func _edges() -> void:
	var world = await _world()
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return

	# Opened at a point beyond the viewport, the panel must still be wholly on screen.
	popup.open_for_sheet(_recorder(), _viewport_size * 4.0)
	await process_frame
	await process_frame
	_check(popup.visible, "the panel opens however far off the click was")
	_check(popup.position.x >= -0.01 and popup.position.y >= -0.01,
			"and never off the top or left edge")
	_check(popup.position.x + popup.size.x <= _viewport_size.x + 0.01
			and popup.position.y + popup.size.y <= _viewport_size.y + 0.01,
			"and never past the bottom or right edge, whatever the viewport")
	_check(popup.covers(popup.get_global_rect().get_center()), "it covers its own middle")
	_check(not popup.covers(popup.get_global_rect().end + Vector2(2.0, 2.0)),
			"and nothing beside it")
	popup.close()
	_check(not popup.covers(popup.get_global_rect().get_center()),
			"a closed panel covers nothing, so it cannot swallow a click")

	# The component schema, drawn without a sheet at all: the same path the live sheet drives,
	# with the rows a real weapon will produce in Task 15.
	popup.render_snapshot({
		"enabled": true,
		"player_controlled": true,
		"can_configure": true,
		"index": 1,
		"supported": [_technique_row(TECHNIQUE_A)],
		"sequence": [],
		"cooldowns": [],
	})
	_check(_has_button(popup, "Add%d" % TECHNIQUE_A),
			"a snapshot rendered without a sheet still offers its additions")
	_check(_any_text(popup, "Add"), "with the addition labelled as one")
	_check(_any_text(popup, "attacks on its own"),
			"and explains an empty sequence rather than drawing nothing at all")
	_check(not _has_button(popup, "Skip0"), "with no sequence rows to draw")

	popup.render_snapshot({})
	_check(_any_text(popup, "No sheet to read"), "an empty snapshot says there is nothing to read")
	_check(not _has_button(popup, "Add%d" % TECHNIQUE_A) and not _has_button(popup, "Skip0"),
			"and leaves no stale controls behind to click")
	await _free(world)
