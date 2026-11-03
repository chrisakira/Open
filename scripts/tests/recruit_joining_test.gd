extends SceneTree

# Task 4 scene test: the Recruit button on the existing character popup, Jane's
# starting flag, and the join that only resolves once the commanded character has
# walked into range. As everywhere else in this suite, the scene presents and sends
# ids; the flag, the range, the attempt and the membership all come back from C++.
#
# New doors are reached by `has_method` and `call`, so a popup or scene that has not
# grown them fails as a counted assertion instead of ending the run.

var failures := 0
var _emissions: Array = []
var _viewport_size := Vector2.ZERO

# Jane is the scene's friendly standing dummy: id 2. The enemy is id 3.
const JANE_ID := 2
const ENEMY_ID := 3


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- fixtures ---------------------------------------------------------------

func _new_world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	return world


func _free_world(world: Node) -> void:
	world.queue_free()
	await process_frame
	await process_frame


func _combat_of(world: Node):
	var combat = world.get("_combat")
	if combat == null:
		_check(false, "the world owns a combat coordinator")
	return combat


func _popup_of(world: Node) -> Control:
	var popups: Array = world.find_children("AttackPopup", "", true, false)
	_check(popups.size() == 1, "the world carries exactly one action popup")
	if popups.size() != 1:
		return null
	return popups[0] as Control


func _button_of(popup: Control, button_name: String) -> Button:
	if popup == null:
		return null
	var found: Array = popup.find_children(button_name, "Button", true, false)
	if found.size() != 1:
		_check(false, "the popup carries exactly one %s, got %d" % [button_name, found.size()])
		return null
	return found[0] as Button


func _visible_actions(popup: Control) -> Array:
	var visible: Array = []
	if popup == null:
		return visible
	for button in popup.find_children("*", "Button", true, false):
		if button is Button and (button as Button).visible:
			visible.append(button)
	return visible


func _recruitable_of(sheet: OpenCharacter) -> bool:
	if sheet == null:
		_check(false, "the sheet exists to be asked about")
		return false
	if not sheet.has_method("is_recruitable"):
		_check(false, "the sheet bridge reports is_recruitable")
		return false
	return bool(sheet.call("is_recruitable"))


func _in_squad_of(sheet: OpenCharacter) -> bool:
	if sheet == null:
		_check(false, "the sheet exists to be read")
		return false
	if not sheet.has_method("is_in_squad"):
		_check(false, "the sheet bridge reports is_in_squad")
		return false
	return bool(sheet.call("is_in_squad"))


func _recruit_condition_of(sheet: OpenCharacter) -> int:
	if sheet == null:
		_check(false, "the sheet exists to be read")
		return -1
	if not sheet.has_method("get_recruit_condition"):
		_check(false, "the sheet bridge reports get_recruit_condition")
		return -1
	return int(sheet.call("get_recruit_condition"))


func _recruit_target_of(sheet: OpenCharacter) -> int:
	if sheet == null:
		_check(false, "the sheet exists to be read")
		return -1
	if not sheet.has_method("get_recruit_target_id"):
		_check(false, "the sheet bridge reports get_recruit_target_id")
		return -1
	return int(sheet.call("get_recruit_target_id"))


func _attack_target_of(sheet: OpenCharacter) -> int:
	if sheet == null:
		_check(false, "the sheet exists to be read")
		return -1
	return int(sheet.get_attack_target_id())


func _record_recruit(target_id: int) -> void:
	_emissions.append(["recruit", target_id])


func _record_attack(target_id: int) -> void:
	_emissions.append(["attack", target_id])


# --- real input -------------------------------------------------------------

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


func _press_key(keycode: int) -> void:
	var key := InputEventKey.new()
	key.keycode = keycode
	key.physical_keycode = keycode
	key.pressed = true
	root.push_input(key)


func _dummy_screen(camera: Camera3D, body: Node) -> Vector2:
	return camera.unproject_position(body.global_position + Vector3(0.0, 0.9, 0.0))


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


# A screen point that really lands on the floor, clear of any open popup.
func _floor_screen(world: Node, camera: Camera3D, anchor: Vector3, popup: Control) -> Vector2:
	for offset in [Vector3(0.0, 0.0, -4.0), Vector3(-5.0, 0.0, 0.0), Vector3(0.0, 0.0, 7.0),
			Vector3(-7.0, 0.0, 3.0), Vector3(7.0, 0.0, 3.0), Vector3(0.0, 0.0, -9.0),
			Vector3(-12.0, 0.0, -6.0), Vector3(3.0, 0.0, 6.0)]:
		var screen: Vector2 = camera.unproject_position(anchor + offset)
		if screen.x < 8.0 or screen.y < 8.0 or screen.x > _viewport_size.x - 8.0 \
				or screen.y > _viewport_size.y - 8.0:
			continue
		if popup != null and popup.visible and popup.get_global_rect().has_point(screen):
			continue
		if _nearest_is_floor(world, camera, screen):
			return screen
	return Vector2.INF


# --- the tests ---------------------------------------------------------------


func _jane_starts_recruitable_and_nobody_else_does() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	_check(jane.sheet.get_character_id() == JANE_ID, "the friendly dummy is Jane's id")
	_check(enemy.sheet.get_character_id() == ENEMY_ID, "the enemy keeps its id")

	_check(_recruitable_of(jane.sheet), "Jane starts recruitable")
	_check(_recruit_condition_of(jane.sheet) == 1, "and her condition is the one that always succeeds")
	_check(not _recruitable_of(enemy.sheet), "the enemy does not start recruitable")
	_check(_recruit_condition_of(enemy.sheet) == 0, "and keeps the default failing condition")
	_check(not _recruitable_of(player.sheet), "the player is the asker, not a recruit target")
	_check(_recruit_condition_of(player.sheet) == 0, "and asks nobody, so keeps no condition of her own")
	for sheet in [jane.sheet, enemy.sheet, player.sheet]:
		_check(not _in_squad_of(sheet), "nobody starts in a squad")
		_check(_recruit_target_of(sheet) == 0, "and nobody starts walking to ask")

	await _free_world(world)


func _popup_offers_recruit_only_for_the_recruitable() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var popup := _popup_of(world)
	if popup == null:
		await _free_world(world)
		return
	var attack := _button_of(popup, "AttackButton")
	var recruit := _button_of(popup, "RecruitButton")
	_check(attack != null, "the popup keeps its Attack button")
	_check(recruit != null, "and grew a Recruit button beside it")
	_check(not popup.visible, "the popup starts closed")
	_check(popup.mouse_filter == Control.MOUSE_FILTER_STOP, "the popup still stops input inside it")
	if recruit != null:
		_check(not recruit.visible, "the Recruit button starts hidden with the panel")

	# Jane: both actions, because her flag says she may be asked.
	_click_at(_dummy_screen(camera, jane))
	_check(popup.visible, "clicking Jane opens the action popup")
	_check(attack != null and attack.visible, "and Attack is offered")
	_check(recruit != null and recruit.visible, "and so is Recruit")
	_check(popup.size.x <= 240.0 and popup.size.y <= 120.0,
			"two buttons still make a tiny panel, not a second sheet: %s" % str(popup.size))
	_check(_recruit_target_of(player.sheet) == 0 and _attack_target_of(player.sheet) == 0,
			"opening the popup issued nothing")
	_check(not _in_squad_of(jane.sheet), "and opening the popup recruited nobody")
	_press_key(KEY_ESCAPE)
	await process_frame
	_check(not popup.visible, "Escape closes the popup")

	# The enemy: Attack only, because no flag means nothing new appears.
	_click_at(_dummy_screen(camera, enemy))
	_check(popup.visible, "clicking the enemy opens the popup too")
	_check(attack != null and attack.visible, "and Attack is offered there as ever")
	_check(recruit != null and not recruit.visible, "but Recruit is not")
	_check(_visible_actions(popup).size() == 1, "so the enemy's popup offers exactly one action")
	_press_key(KEY_ESCAPE)
	await process_frame

	await _free_world(world)


func _recruit_presses_the_ask_and_is_not_a_floor_click() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var popup := _popup_of(world)
	var recruit := _button_of(popup, "RecruitButton")
	if popup == null or recruit == null:
		await _free_world(world)
		return
	_emissions.clear()
	if popup.has_signal("recruit_requested"):
		popup.recruit_requested.connect(_record_recruit)
	else:
		_check(false, "the action popup announces recruit_requested")

	# One frame between the open and the press: a panel that grows an action it had not
	# shown before lays its buttons out on that frame, not on the click.
	_click_at(_dummy_screen(camera, jane))
	await process_frame
	_check(popup.visible, "Jane's popup is open")
	_check(recruit.visible, "and Recruit is offered")
	var before: Vector3 = player.global_position
	_click_at(recruit.get_global_rect().get_center())

	_check(_emissions.size() == 1, "pressing Recruit emits exactly once")
	_check(_emissions.size() == 1 and int(_emissions[0][1]) == JANE_ID,
			"and carries Jane's id")
	_check(not popup.visible, "and closes the popup")
	_check(_recruit_target_of(player.sheet) == JANE_ID, "the scene sent the ask to C++")
	_check(_attack_target_of(player.sheet) == 0, "and an ask is not an attack")
	_check(not marker.visible, "the Recruit click created no move marker")
	_check(player.global_position == before, "and the click did not walk the player anywhere")
	_check(not _in_squad_of(jane.sheet), "the ask is not resolved on the click")
	_check(_recruitable_of(jane.sheet), "and the flag stays until the answer")
	_check(popup.get_global_rect().size.length() > 0.0, "a closed popup still covers its own area")

	# Out of range, the coordinator walks: this is C++'s order read through the scene.
	var ordered: Vector3 = _combat_of(world).velocity_at(player.sheet.get_character_id())
	_check(ordered.length() > 0.0, "the recruiter is told to walk toward Jane")
	_check(_recruit_target_of(player.sheet) == JANE_ID, "and the ask is kept while the walk is on")

	await _free_world(world)


func _walking_in_joins_jane_and_hides_the_button() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var popup := _popup_of(world)
	var recruit := _button_of(popup, "RecruitButton")
	var combat = _combat_of(world)
	if popup == null or recruit == null or combat == null:
		await _free_world(world)
		return

	_click_at(_dummy_screen(camera, jane))
	_click_at(recruit.get_global_rect().get_center())
	_check(_recruit_target_of(player.sheet) == JANE_ID, "the ask is held out of range")

	# The walk closes the distance the way the scene would, and one physics frame is
	# enough to answer: positions are published from the nodes, then the coordinator
	# steps once.
	player.global_position = jane.global_position + Vector3(1.0, 0.0, 0.0)
	await physics_frame
	await physics_frame

	_check(_in_squad_of(jane.sheet), "Jane joined")
	_check(jane.sheet.get_faction() == 0, "and she stays Friendly")
	_check(not _recruitable_of(jane.sheet), "her flag is spent")
	_check(_recruit_target_of(player.sheet) == 0, "the recruiter was stood down")
	_check(_attack_target_of(player.sheet) == 0, "and no fight started")
	_check(combat.velocity_at(player.sheet.get_character_id()) == Vector3.ZERO,
			"a stood-down recruiter walks on nothing")

	# The spent flag is what the popup reads: the same click now offers Attack only.
	_click_at(_dummy_screen(camera, jane))
	_check(popup.visible, "Jane's popup opens again")
	_check(recruit != null and not recruit.visible, "and Recruit is gone from it")
	_check(_visible_actions(popup).size() == 1, "leaving exactly one action")
	_check(_recruit_target_of(player.sheet) == 0, "and reopening the popup asks nothing")
	_check(_in_squad_of(jane.sheet), "opened as she is now, Jane stays a member")

	await _free_world(world)


func _floor_click_cancels_a_pending_ask_without_joining() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var popup := _popup_of(world)
	var recruit := _button_of(popup, "RecruitButton")
	if popup == null or recruit == null:
		await _free_world(world)
		return

	_click_at(_dummy_screen(camera, jane))
	_click_at(recruit.get_global_rect().get_center())
	_check(_recruit_target_of(player.sheet) == JANE_ID, "the ask is held out of range")

	var floor_point := _floor_screen(world, camera, jane.global_position, popup)
	_check(floor_point != Vector2.INF, "a floor point outside the popup is reachable")
	_click_at(floor_point)
	_check(not _in_squad_of(jane.sheet), "the walk-to-ask cancelled without joining")
	_check(_recruit_target_of(player.sheet) == 0, "and the ask is gone")
	_check(_recruitable_of(jane.sheet), "the flag is unspent, because nothing refused")
	_check(_in_squad_of(jane.sheet) == false, "still nobody in a squad")
	_check(marker.visible, "the floor click showed its marker as a move does")
	var ordered: Vector3 = player.sheet.velocity_at(player.global_position)
	_check(ordered.length() > 0.0, "and the player walks to the click")

	await _free_world(world)


func _attack_still_works_on_everyone() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var jane: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var popup := _popup_of(world)
	var attack := _button_of(popup, "AttackButton")
	if popup == null or attack == null:
		await _free_world(world)
		return

	# The enemy, who never grew a flag: Attack still orders the fight.
	_click_at(_dummy_screen(camera, enemy))
	_click_at(attack.get_global_rect().get_center())
	_check(_attack_target_of(player.sheet) == ENEMY_ID, "Attack still orders the enemy")
	_check(_recruit_target_of(player.sheet) == 0, "and no ask came with it")
	_press_key(KEY_ESCAPE)
	await process_frame

	# Jane, who carries both buttons: Attack orders her, and the ask that was pending
	# goes away without ever resolving (docs/recruiting.md "Attack while recruiting").
	# One frame between the open and the press, because a popup that grows an action
	# it had not shown before lays its buttons out on that frame, not on the click.
	_click_at(_dummy_screen(camera, jane))
	var recruit := _button_of(popup, "RecruitButton")
	_check(recruit != null, "Jane's popup grew a Recruit button")
	if recruit == null:
		await _free_world(world)
		return
	await process_frame
	_check(recruit.visible, "Jane offers Recruit first")
	_click_at(recruit.get_global_rect().get_center())
	_check(_recruit_target_of(player.sheet) == JANE_ID, "the ask is held")
	_click_at(_dummy_screen(camera, jane))
	await process_frame
	_click_at(attack.get_global_rect().get_center())
	_check(_attack_target_of(player.sheet) == JANE_ID,
			"Attack orders her: got %d, popup visible %s" % [_attack_target_of(player.sheet),
					str(popup.visible)])
	_check(_recruit_target_of(player.sheet) == 0, "and the ask is gone")
	_check(_recruitable_of(jane.sheet), "with her flag unspent")
	_check(not _in_squad_of(jane.sheet), "and nobody joined")

	await _free_world(world)


func _run() -> void:
	_viewport_size = root.get_visible_rect().size
	_check(_viewport_size.x > 0.0 and _viewport_size.y > 0.0, "the test viewport has a size")

	await _jane_starts_recruitable_and_nobody_else_does()
	await _popup_offers_recruit_only_for_the_recruitable()
	await _recruit_presses_the_ask_and_is_not_a_floor_click()
	await _walking_in_joins_jane_and_hides_the_button()
	await _floor_click_cancels_a_pending_ask_without_joining()
	await _attack_still_works_on_everyone()

	if failures == 0:
		print("Recruit joining: all checks passed")
	quit(1 if failures else 0)
