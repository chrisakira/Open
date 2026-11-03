extends SceneTree

# Task 6 scene tests. Left-clicking a living dummy opens one tiny action popup;
# pressing Attack sends a C++ order through the coordinator, a floor click still
# moves and cancels, and the world owns exactly one combat/regeneration tick per
# physics frame. Every combat number (damage, reach, cooldown, in_combat) must come
# from C++: nothing in the scene decides it. The numbers are now the typed ones, which
# changes what a scene test can watch: a rank-1 unarmed blow is 8 x 1/200 over the
# target's Toughness (0.04 / 1.05) when nothing answers it, and a defence that is ready
# and paid for turns it aside entirely, so the first swing of a fresh fight does no
# damage at all. Landed damage is therefore watched with two attackers on one target.
#
# The popup and the coordinator are reached by node lookup and duck-typed calls,
# so a missing AttackPopup or missing scene wiring fails as an assertion here
# instead of a parse error.

var failures := 0
var _emissions: Array = []

var _viewport_size := Vector2.ZERO

# What one rank-1 unarmed blow costs a sheet that cannot answer it. The arithmetic is
# pinned in C++; this suite only needs the amount to say "the number that arrived is the
# typed one" and to insist that damage arrives in whole blows rather than fragments.
const TYPED_BLOW := 0.04 / 1.05


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


func _attack_popup_of(world: Node) -> Control:
	var popups: Array = world.find_children("AttackPopup", "", true, false)
	_check(popups.size() == 1, "the world carries exactly one attack popup")
	if popups.size() != 1:
		return null
	return popups[0] as Control


func _open_for_target(popup: Control, target_id: int, screen: Vector2) -> void:
	if popup == null:
		return
	if not popup.has_method("open_for_target"):
		_check(false, "the attack popup offers open_for_target")
		return
	popup.call("open_for_target", target_id, screen)


func _covers(popup: Control, point: Vector2) -> bool:
	if popup == null:
		return false
	if not popup.has_method("covers"):
		_check(false, "the attack popup reports covers(point)")
		return false
	return bool(popup.call("covers", point))


# The panel is no longer one button tall: a recruitable character's popup stacks a
# second action, so "press Attack" means that button's own rectangle rather than the
# middle of the panel - which, with two buttons, belongs to neither.
func _named_button(popup: Control, button_name: String) -> Button:
	if popup == null:
		return null
	var found: Array = popup.find_children(button_name, "Button", true, false)
	if found.size() != 1:
		_check(false, "the popup carries exactly one %s, got %d" % [button_name, found.size()])
		return null
	return found[0] as Button


func _visible_actions(popup: Control) -> Array:
	var shown: Array = []
	if popup == null:
		return shown
	for child in popup.find_children("*", "Button", true, false):
		if child is Button and child.visible:
			shown.append(child)
	return shown


# Pressing means the button's own rectangle, which is only true after the panel has
# laid its children out: on the frame a popup opens, its children still carry the
# rectangle of the last thing it showed, and a panel that grew a second action would
# otherwise be pressed on the wrong action. So this coroutine takes that frame, and the
# tests await it.
func _press_attack(popup: Control) -> void:
	var attack := _named_button(popup, "AttackButton")
	if attack == null:
		_check(false, "the popup has an Attack button to press")
		return
	await process_frame
	_click_at(attack.get_global_rect().get_center())


func _tick(node: Node, delta: float) -> void:
	if not node.has_method("_physics_process"):
		_check(false, "%s has a physics tick to drive" % node.name)
		return
	node.call("_physics_process", delta)


func _record_attack(target_id: int) -> void:
	_emissions.append(target_id)


# --- real input -------------------------------------------------------------

# Viewport-local mouse press/release through Godot's GUI and unhandled-input
# pipeline, the same route a player's click takes.
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


# The picker must reach a character before the floor behind it, so a floor probe
# has to name the collider a real click would hit first.
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
func _floor_screen(world: Node, camera: Camera3D, anchor: Vector3, popup: Control) -> Vector2:
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


func _parts_lost(sheet: OpenCharacter) -> Array:
	var changed := 0
	var total := 0.0
	for part in range(6):
		var lost: float = sheet.get_part_max_health(part) - sheet.get_part_health(part)
		if not _close(lost, 0.0):
			changed += 1
			total += lost
	return [changed, total]


func _wait_for_stamina_dip(sheet: OpenCharacter, limit: int) -> int:
	# Frames until this sheet's bar next falls below full: the only way to see a swing or
	# a defence that did no damage. Both cost 8 and neither can afford to be twice as
	# frequent as the bar regrows, so a dip is one action. -1 when it never came.
	var guard := 0
	while guard < limit and sheet.get_stamina() > 99.0:
		await physics_frame
		guard += 1
	return guard if guard < limit else -1


func _attack_target_id(sheet: OpenCharacter) -> int:
	if not sheet.has_method("get_attack_target_id"):
		_check(false, "the sheet bridge reports its attack target")
		return -1
	return int(sheet.get_attack_target_id())


# --- real-input picking -----------------------------------------------------

func _living_dummy_click_opens_attack() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	_check(camera != null and marker != null, "the world still owns its camera and marker")

	# Opening a popup must not issue or cancel anything, so the sheet keeps a
	# live move order through the whole click.
	player.go_to(player.global_position + Vector3(0.0, 0.0, 5.0))
	await physics_frame
	marker.visible = false

	for dummy in [ally, enemy]:
		var popup := _attack_popup_of(world)
		if popup == null:
			break
		_check(not popup.visible, "%s's action popup is closed before this pick" % dummy.name)
		_check(popup.mouse_filter == Control.MOUSE_FILTER_STOP, "the action popup stops input inside it")
		_check(popup.size.x <= 240.0 and popup.size.y <= 120.0,
			"the action popup is a tiny panel, not a second sheet")
		# Attack is the action every living character offers. A second action may
		# appear beside it for a character whose flag says she can be asked to join;
		# that is recruiting's own test (`recruit_joining_test.gd`), and the one thing
		# it must never do here is replace or hide the fight.
		var attack_button := _named_button(popup, "AttackButton")
		_check(attack_button != null, "%s's popup carries an Attack action" % dummy.name)

		_click_at(_dummy_screen(camera, dummy))
		_check(popup.visible, "clicking the living %s opens its action popup" % dummy.name)
		_check(attack_button != null and attack_button.visible,
				"and the open popup still offers Attack")
		# The second action is recruiting's, and this suite keeps its own ground:
		# an unaskable dummy is offered exactly the one action it always was, and a
		# askable one gains the ask without the fight being replaced.
		var expected := 2 if dummy == ally else 1
		var offered := _visible_actions(popup)
		_check(offered.size() == expected,
				"%s's popup offers %d action(s), got %d" % [dummy.name, expected, offered.size()])
		var names: Array = []
		for action in offered:
			names.append(String((action as Button).text).to_lower())
		_check(names.has("attack"), "%s's popup never lost its Attack" % dummy.name)
		_check(names.has("recruit") == (dummy == ally),
				"%s's popup offers Recruit only where the flag says it may" % dummy.name)
		_check(not marker.visible, "a character click shows no move marker")
		_check(_attack_target_id(player.sheet) == 0, "a character click issues no attack yet")
		_check(player.sheet.velocity_at(player.global_position).length() > 0.0,
			"and leaves the existing move order alone")

		# Dismiss it so the next pick is a fresh one, and so the following click
		# cannot land on the panel.
		_press_key(KEY_ESCAPE)
		await process_frame

	var closed := _attack_popup_of(world)
	player.sheet.clear_order()
	marker.visible = false
	await physics_frame

	# The player's own body is not a target, and neither is a corpse.
	if closed != null:
		_click_at(_dummy_screen(camera, player))
		_check(not closed.visible, "clicking the player opens no action popup")
		_check(not marker.visible, "and the player's body does not fall through to the floor")
		_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO,
			"so no move order reaches C++")

		_check(ally.sheet.apply_damage(1, 100.0), "the friendly dummy is killed through the torso")
		_check(not ally.sheet.is_alive(), "and is dead")
		_click_at(_dummy_screen(camera, ally))
		_check(not closed.visible, "clicking a dead dummy opens no action popup")
		_check(not marker.visible, "a dead dummy does not fall through to the floor behind it")
		_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO,
			"so a corpse click orders nothing")
		_check(_attack_target_id(ally.sheet) == 0 and _attack_target_id(enemy.sheet) == 0,
			"and no click ever orders the standing dummies")

	await _free_world(world)


# --- popup isolation --------------------------------------------------------

func _attack_button_is_not_floor_input() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup != null and combat != null:
		_emissions.clear()
		if popup.has_signal("attack_requested"):
			popup.attack_requested.connect(_record_attack)
		else:
			_check(false, "the attack popup announces attack_requested")
		var enemy_hp_before: float = enemy.sheet.get_health()

		_click_at(_dummy_screen(camera, enemy))
		_check(popup.visible, "the Enemy click opened its popup")
		_check(_covers(popup, popup.get_global_rect().get_center()), "the popup covers its own area")
		await _press_attack(popup)

		_check(_emissions.size() == 1, "pressing Attack emits exactly once")
		_check(_emissions.size() == 1 and int(_emissions[0]) == 3, "and carries the pending character id")
		_check(not popup.visible, "and closes the popup")
		_check(_attack_target_id(player.sheet) == 3, "the click became a C++ attack order")
		if combat.is_in_combat(1) and combat.is_in_combat(3):
			_check(true, "C++ put both characters in combat")
		else:
			_check(false, "the confirmed attack puts attacker and target in combat")
		_check(not marker.visible, "the Attack click created no move marker")
		_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO,
			"and no walk order either")
		_check(_close(enemy.sheet.get_health(), enemy_hp_before),
			"confirming alone swings at nothing")

		# The popup's own click must not be mistaken for a floor click.
		_check(_attack_target_id(enemy.sheet) == 0, "the defender was never ordered")

	await _free_world(world)


func _dismiss_and_stale_target() -> void:
	await _escape_closes_without_ordering()
	await _outside_click_dismisses_and_moves()
	await _second_dummy_replaces_pending_target()
	await _killed_target_is_not_a_fight()
	await _popup_stays_inside_the_viewport()
	await _character_sheet_clicks_do_not_reach_the_picker()


func _escape_closes_without_ordering() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup != null and combat != null:
		_click_at(_dummy_screen(camera, enemy))
		_check(popup.visible, "the Enemy click opened its popup")
		_press_key(KEY_ESCAPE)
		await process_frame
		_check(not popup.visible, "Escape closes the attack popup")
		_check(_attack_target_id(player.sheet) == 0, "Escape issued no order")
		_check(not marker.visible, "and showed no marker")
		_check(not combat.is_in_combat(1), "so nobody is fighting")
	await _free_world(world)


func _outside_click_dismisses_and_moves() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup != null and combat != null:
		_click_at(_dummy_screen(camera, enemy))
		_check(popup.visible, "the Enemy click opened its popup")
		var floor_point := _floor_screen(world, camera, player.global_position, popup)
		_check(floor_point != Vector2.INF, "a floor point outside the popup is reachable on screen")
		if floor_point != Vector2.INF:
			_click_at(floor_point)
			_check(not popup.visible, "an outside world click dismisses the popup")
			_check(marker.visible, "and that same click still marks the floor")
			_check(player.sheet.velocity_at(player.global_position).length() > 0.0,
				"and still becomes a move order")
			_check(_attack_target_id(player.sheet) == 0, "while issuing no attack")
			await physics_frame
			_check(not combat.is_in_combat(1) and not combat.is_in_combat(3),
				"and nobody entered combat")
	await _free_world(world)


func _second_dummy_replaces_pending_target() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")

	var popup := _attack_popup_of(world)
	if popup != null:
		_emissions.clear()
		if popup.has_signal("attack_requested"):
			popup.attack_requested.connect(_record_attack)
		_click_at(_dummy_screen(camera, enemy))
		_check(popup.visible, "the first dummy opened the popup")
		_click_at(_dummy_screen(camera, ally))
		_check(popup.visible, "the second dummy keeps one popup open")
		_check(_emissions.is_empty(), "choosing a target never emits Attack")
		await _press_attack(popup)
		_check(_emissions.size() == 1 and int(_emissions[0]) == 2,
			"the last dummy chosen is the one confirmed")
		_check(_attack_target_id(player.sheet) == 2, "so C++ holds the Friendly as the target")
		_check(_attack_target_id(enemy.sheet) == 0, "and the skipped dummy was never involved")
		_check(not marker.visible, "with no move marker anywhere")
	await _free_world(world)


func _killed_target_is_not_a_fight() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup != null and combat != null:
		_emissions.clear()
		if popup.has_signal("attack_requested"):
			popup.attack_requested.connect(_record_attack)
		_click_at(_dummy_screen(camera, enemy))
		_check(popup.visible, "the popup is open for the Enemy")
		_check(enemy.sheet.apply_damage(1, 100.0), "the target dies after the popup opened")
		_check(not enemy.sheet.is_alive(), "C++ agrees it is dead")
		await _press_attack(popup)
		_check(_emissions.size() == 1, "the stale confirmation still emits once")
		_check(not popup.visible, "and closes")
		_check(_attack_target_id(player.sheet) == 0, "C++ refused the dead target")
		_check(not combat.is_in_combat(1) and not combat.is_in_combat(3),
			"and no fight started")
	await _free_world(world)


func _popup_stays_inside_the_viewport() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup != null and combat != null:
		for corner in [Vector2(_viewport_size.x - 2.0, _viewport_size.y - 2.0),
				Vector2(_viewport_size.x - 40.0, 6.0), Vector2(4.0, _viewport_size.y - 30.0),
				Vector2(-20.0, -20.0)]:
			_open_for_target(popup, 2, corner)
			await process_frame
			var rect := popup.get_global_rect()
			_check(rect.position.x >= -0.01 and rect.position.y >= -0.01,
				"the popup stays on screen at %s" % corner)
			_check(rect.end.x <= _viewport_size.x + 0.01 and rect.end.y <= _viewport_size.y + 0.01,
				"and never runs past the viewport at %s" % corner)
			_check(rect.size.x > 20.0 and rect.size.y > 12.0, "and stays large enough to click")
			_click_at(rect.get_center())
			_check(_attack_target_id(player.sheet) == 2, "its button is clickable at %s" % corner)
			_check(not popup.visible, "and closes after the click")
			player.sheet.clear_order()
	await _free_world(world)


func _character_sheet_clicks_do_not_reach_the_picker() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var sheets: Array = world.find_children("CharacterPopup", "", true, false)
	_check(sheets.size() == 1, "the character sheet is still in the world")

	var popup := _attack_popup_of(world)
	if sheets.size() == 1 and popup != null:
		var sheet: Control = sheets[0] as Control
		_press_key(KEY_I)
		await process_frame
		await process_frame
		_check(sheet.visible, "I still opens the character sheet")
		_click_at(sheet.get_global_rect().get_center())
		_check(sheet.visible, "a click inside the sheet stays inside it")
		_check(not popup.visible, "and does not reach the character picker")
		_check(not marker.visible, "and is not a floor click")
		_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO,
			"so it orders nothing")
		var outside := _floor_screen(world, camera, player.global_position, sheet)
		# A skipped check is not a passed check: if no uncovered floor point can be
		# found, this test has stopped testing the thing it is for.
		_check(outside != Vector2.INF, "a floor point clear of the open character sheet exists")
		if outside != Vector2.INF:
			_click_at(outside)
			_check(marker.visible, "a click outside the sheet still marks the floor")
		_press_key(KEY_I)
		_check(not sheet.visible, "I closes the sheet again")
		_check(not popup.visible, "and no action popup is left behind")
	await _free_world(world)


# --- the fight itself -------------------------------------------------------

func _approach_then_auto_swing() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup == null or combat == null:
		await _free_world(world)
		return

	var enemy_start: Vector3 = enemy.global_position
	var player_start: Vector3 = player.global_position
	var enemy_hp: float = enemy.sheet.get_health()
	_check(enemy_hp > 99.99, "the Enemy starts unwounded")

	_click_at(_dummy_screen(camera, enemy))
	_check(popup.visible, "the Enemy click opened its popup")
	await _press_attack(popup)
	_check(_attack_target_id(player.sheet) == 3, "Attack ordered the player through C++")
	_check(combat.is_in_combat(1), "the coordinator says the attacker is fighting")

	# Approach: walk up to the target, bounded so a stall fails instead of hangs. The
	# wait is for the gap and not for damage: under the typed rules the opening swing is
	# turned aside, so health says nothing about whether the fight arrived.
	var guard := 0
	var gap := horizontal_gap(player.global_position, enemy.global_position)
	while guard < 240 and gap > 1.5 + 0.001:
		await physics_frame
		guard += 1
		gap = horizontal_gap(player.global_position, enemy.global_position)
	_check(guard < 240, "the player closed to melee reach within 240 physics frames")
	if guard >= 240:
		await _free_world(world)
		return

	_check(player.global_position.distance_to(player_start) > 1.0, "the player really approached")
	_check(gap <= 1.5 + 0.001, "and stopped inside melee range, not beyond it")
	_check(gap > 0.8, "without walking into the dummy")

	# The opening swing, seen through the two bars it spent. A fresh Enemy has a ready
	# dodge and the stamina to pay for it, and a defence that is ready and paid for is
	# worth everything it can do, so the blow lands as nothing: no health, no body part.
	guard = await _wait_for_stamina_dip(enemy.sheet, 240)
	_check(guard >= 0, "the Enemy answered a swing within 240 physics frames of the fight arriving")
	_check(enemy.sheet.get_stamina() < 85.5,
			"the Enemy paid 8 for the answer and 8 more for the return it provoked")
	_check(player.sheet.get_stamina() < 85.5,
			"the player paid 8 for the swing and 8 for dodging that return")
	_check(_close(enemy.sheet.get_health(), enemy_hp), "and the turned-aside blow changed no health")
	_check(int(_parts_lost(enemy.sheet)[0]) == 0, "and no body part at all")
	_check(player.sheet.get_level() == 1, "the player is still level one")
	_check(_attack_target_id(enemy.sheet) == 1, "the swung-at Enemy ordered the player back")
	# Facing is presentation reading the order: the dummy cannot walk to its target, but a
	# model holding an attack order turns its eyes on it. The turn is smoothed, so give it
	# half a second before measuring; the enemy spawned facing away from the fight and must
	# not still be.
	for frame in range(30):
		await physics_frame
	var facing: Vector3 = (-enemy.global_transform.basis.z).normalized()
	var toward_player: Vector3 = player.global_position - enemy.global_position
	toward_player.y = 0.0
	toward_player = toward_player.normalized()
	_check(facing.dot(toward_player) > 0.95,
			"and the Enemy faces the player it is fighting, got %.2f" % facing.dot(toward_player))
	_check(_close(enemy.global_position.x - enemy_start.x, 0.0) and \
			_close(enemy.global_position.z - enemy_start.z, 0.0), "the standing dummy did not move")
	_check(not enemy.sheet.get_can_move(), "and it still cannot move on its own")
	_check(_close(player.sheet.get_health(), 100.0), "the return blow was dodged whole")

	# Automatic repeat: no further clicks, just the scene clock passing a cooldown. The
	# bar refills well before the next swing is due, so a second dip can only be a second
	# exchange, and the only thing that drives it is the world's own frame loop. The
	# Enemy now spends 16 a round - answer and retaliation - so it starts the refill
	# deeper than the 8 an unengaged defender used to cost.
	guard = 0
	while guard < 240 and enemy.sheet.get_stamina() < 99.0:
		await physics_frame
		guard += 1
	_check(guard < 240, "the Enemy's bar came back while the cooldown ran")
	guard = await _wait_for_stamina_dip(enemy.sheet, 300)
	_check(guard >= 0, "the fight swung again on its own after one cooldown")
	_check(guard >= 0 and enemy.sheet.get_stamina() < 93.0, "and the Enemy paid for the answer again")
	_check(_close(enemy.sheet.get_health(), enemy_hp), "still turned aside, so still no damage")
	_check(_close(player.sheet.get_health(), 100.0), "and the player is still unhurt")
	_check(_attack_target_id(enemy.sheet) == 1, "the Enemy still holds its retaliation order")

	# A floor click cancels and moves.
	var floor_point := _floor_screen(world, camera, player.global_position, popup)
	_check(floor_point != Vector2.INF, "a floor point is reachable after the fight")
	if floor_point != Vector2.INF:
		_click_at(floor_point)
		_check(_attack_target_id(player.sheet) == 0, "the floor click cancelled the attack")
		_check(marker.visible, "and showed the movement marker")
		# Walking away cancels the player's own order; it does not cancel the Enemy's
		# grudge, and combat is true for both sides of any live order. The player is
		# still being attacked, so the player is still in a fight - by order, not by
		# distance, which is what the docs say in_combat means.
		_check(combat.is_in_combat(1), "but the provoked Enemy keeps the player in the fight")
		_check(combat.is_in_combat(3), "and still holds its own retaliation order")
		var stopped_hp: float = enemy.sheet.get_health()
		var walked: Vector3 = player.global_position
		for frame in range(40):
			await physics_frame
		_check(player.global_position.distance_to(walked) > 0.5, "the player walked away")
		_check(_close(enemy.sheet.get_health(), stopped_hp), "and nothing hit the old target again")

	await _free_world(world)


func _friendly_target_proves_no_faction_gate() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var combat = _combat_of(world)

	var popup := _attack_popup_of(world)
	if popup == null or combat == null:
		await _free_world(world)
		return

	_click_at(_dummy_screen(camera, ally))
	_check(popup.visible, "clicking the Friendly dummy opens the same popup")
	await _press_attack(popup)
	_check(_attack_target_id(player.sheet) == 2, "C++ holds the Friendly as the target")
	_check(combat.is_in_combat(1) and combat.is_in_combat(2), "both are in combat")

	var guard := 0
	var gap := horizontal_gap(player.global_position, ally.global_position)
	while guard < 240 and gap > 1.5 + 0.001:
		await physics_frame
		guard += 1
		gap = horizontal_gap(player.global_position, ally.global_position)
	_check(guard < 240, "the player closed on the Friendly within 240 physics frames")
	if guard < 240:
		_check(gap <= 1.5 + 0.001, "and stopped in range")
		# Faction changes nothing about the order, and nothing about the answer either: the
		# Friendly defends itself exactly as the Enemy does, which is to say the first blow
		# of the fight is turned aside and paid for.
		guard = await _wait_for_stamina_dip(ally.sheet, 240)
		_check(guard >= 0, "the Friendly answered a swing within 240 physics frames")
		_check(ally.sheet.get_stamina() < 93.0, "and paid the same 8 for the privilege")
		_check(_close(ally.sheet.get_health(), 100.0), "with the same result: no damage at all")
	await _free_world(world)


func horizontal_gap(a: Vector3, b: Vector3) -> float:
	return Vector2(a.x - b.x, a.z - b.z).length()


# --- one clock owner --------------------------------------------------------

func _scene_uses_combat_regen_once() -> void:
	await _manual_tick_regeners_once()
	await _positions_reach_combat_each_frame()


func _manual_tick_regeners_once() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var combat = _combat_of(world)
	if combat == null:
		await _free_world(world)
		return
	_check(player.get("combat") == combat, "the player was handed the coordinator")
	_check(ally.get("combat") == combat, "and so was the Friendly")
	_check(enemy.get("combat") == combat, "and the Enemy")

	# Controlled fixture: stand the player just inside reach and drive the world
	# by hand, so exactly one 0.1 s frame is being measured. Nothing here depends on
	# whether the blow landed: what one frame did is visible in the two bars.
	ally.global_position = Vector3(-4.0, 0.1, -2.0)
	player.global_position = Vector3(-2.7, 0.1, -2.0)
	await physics_frame
	await physics_frame
	world.set_physics_process(false)
	player.set_physics_process(false)
	ally.set_physics_process(false)
	enemy.set_physics_process(false)

	_check(combat.issue_attack(1, 2), "the in-range attack order is accepted")
	_tick(world, 0.1)
	# The swing happened on this tick and the ally's ready, paid dodge turned it aside -
	# and the ally, provoked, ordered the player back and returned the blow inside the
	# same tick, at an answer the player paid for as well. The tick is measured in bars:
	# 100 - 8 - 8 + one combat frame's 0.5 for each of them.
	_check(_close(ally.sheet.get_health(), 100.0), "the manual tick's opening swing was turned aside")
	_check(int(_parts_lost(ally.sheet)[0]) == 0, "and wounded no body part")
	var after_swing: float = player.sheet.get_stamina()
	_check(_close(after_swing, 84.5, 0.0001),
			"the player paid 8 for the swing and 8 for the provoked return, and regrew 0.5")
	_check(_close(ally.sheet.get_stamina(), 84.5, 0.0001),
			"the ally paid 8 for the defence and 8 for its retaliation, and regrew the same 0.5")
	var ally_stamina: float = ally.sheet.get_stamina()
	var ally_mana: float = ally.sheet.get_mana()
	var enemy_stamina: float = enemy.sheet.get_stamina()
	var ally_hp: float = ally.sheet.get_health()
	var player_hp: float = player.sheet.get_health()

	# Exactly one more world tick plus the actor callbacks that follow it.
	_tick(world, 0.1)
	_tick(player, 0.1)
	_tick(ally, 0.1)
	_tick(enemy, 0.1)
	_check(_close(player.sheet.get_stamina() - after_swing, 0.5, 0.0001),
		"one combat frame regrew 0.5 stamina, exactly once")
	_check(_close(ally.sheet.get_stamina() - ally_stamina, 0.5, 0.0001),
		"and the sheet that spent a defence regrows it, once, like anybody else")
	_check(_close(ally.sheet.get_mana(), ally_mana), "with its untouched mana still clamped at max")
	_check(_close(enemy.sheet.get_stamina(), enemy_stamina), "the bystander's pools stayed full")
	_check(_close(ally.sheet.get_health(), ally_hp), "the still-cooling attack did not spend again")
	_check(_close(player.sheet.get_health(), player_hp), "and every blow this tick was turned aside")
	_check(_attack_target_id(ally.sheet) == 1, "the provoked ally now holds its attacker as its order")
	_check(_attack_target_id(enemy.sheet) == 0, "the bystander gained no intent of any kind")

	await _free_world(world)


func _two_attackers_land_whole_blows() -> void:
	# A landed blow, seen through the scene. One rank-1 attacker can never land on a fresh
	# rank-1 defender: they refresh on the same clock and every answer is paid for. With a
	# second attacker on the same target, the target answers one and the other arrives
	# while that answer is cooling, whole, at the typed amount, once per cooldown and with
	# no further input. Watching for that is how this suite proves the scene is running the
	# C++ clock rule rather than showing damage it decided itself.
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	var combat = _combat_of(world)
	if combat == null:
		await _free_world(world)
		return

	# Fixture: both attackers already inside reach of the Enemy, so nobody has to walk and
	# the two swings are gated by nothing but their own cooldowns.
	ally.global_position = enemy.global_position + Vector3(0.0, 0.0, 0.9)
	player.global_position = enemy.global_position + Vector3(0.9, 0.0, 0.0)
	await physics_frame
	await physics_frame
	_check(combat.issue_attack(1, 3), "the player is ordered at the Enemy")
	_check(combat.issue_attack(2, 3), "and the Friendly is ordered at the same Enemy")
	_check(combat.is_in_combat(2), "so the Friendly is in the fight as an attacker")

	var enemy_hp: float = enemy.sheet.get_health()
	var drops := 0
	var whole_blows := 0
	var guard := 0
	while guard < 600 and drops < 3:
		await physics_frame
		guard += 1
		var lost: float = enemy_hp - enemy.sheet.get_health()
		if lost <= 0.0001:
			continue
		drops += 1
		enemy_hp = enemy.sheet.get_health()
		var blows := int(roundf(lost / TYPED_BLOW))
		if _close(lost, blows * TYPED_BLOW, 0.0002):
			whole_blows += blows
		_check(_close(lost, blows * TYPED_BLOW, 0.0002),
			"a drop is a whole number of unanswered typed blows, never a fragment")
	_check(drops >= 3, "blows kept landing on their own, with no further input")
	_check(_close(100.0 - enemy.sheet.get_health(), whole_blows * TYPED_BLOW, 0.0002),
		"and the Enemy's whole wound count is exactly the blows that arrived")
	var parts: Array = _parts_lost(enemy.sheet)
	_check(int(parts[0]) >= 1, "at least one body part took them")
	_check(_close(float(parts[1]), whole_blows * TYPED_BLOW, 0.0002),
		"the body parts were charged once for the same damage main HP was")
	_check(player.sheet.get_health() > 99.0 and ally.sheet.get_health() > 99.0,
		"the Enemy's returns land at most typed quanta, never a rout")
	_check(_attack_target_id(enemy.sheet) == 1,
		"and the Enemy fought back: its order names the first sheet that swung at it")
	_check(combat.remove_character(2), "the second attacker is stood down again")
	await _free_world(world)


func _positions_reach_combat_each_frame() -> void:
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var combat = _combat_of(world)
	if combat == null:
		await _free_world(world)
		return

	_check(world.process_physics_priority < player.process_physics_priority,
		"the world publishes positions before the actors tick")

	# Out of reach, so the only motion the coordinator can ask for is pursuit.
	_check(combat.issue_attack(1, 2), "an out-of-range order is accepted")
	var stale: Vector3 = combat.velocity_at(1)
	_check(stale.x < 0.0, "the coordinator first aims from the position it was given")
	player.global_position = ally.global_position + Vector3(0.0, 0.0, -9.0)
	await physics_frame
	var chase: Vector3 = combat.velocity_at(1)
	_check(chase.z > 3.0 and absf(chase.x) < 0.5 and _close(chase.y, 0.0),
		"the position published this frame feeds C++ pursuit, not the old one")
	_check(Vector2(player.velocity.x, player.velocity.z).length() > 0.0,
		"the actor consumed that velocity in the same frame")
	_check(_close(horizontal_gap(player.global_position, ally.global_position), 9.0, 0.2),
		"and the gap C++ sees is the one the scene reports")
	_check(camera != null, "the camera is still the world's own")

	player.sheet.clear_order()
	await _free_world(world)


func _popup_ignores_a_non_positive_target_id() -> void:
	# The popup stores only an id, so an id that cannot name a character must not
	# produce a panel that looks live. Break this catch: open_for_target showing the
	# panel before it checks the id, leaving an Attack button that does nothing.
	var world := await _new_world()
	var popup := _attack_popup_of(world)
	if popup == null:
		await _free_world(world)
		return
	for bad in [0, -1, -100]:
		_open_for_target(popup, bad, Vector2(120.0, 120.0))
		_check(not popup.visible, "target id %d opens no panel" % bad)
		_check(not _covers(popup, Vector2(120.0, 120.0)), "and the invisible panel claims no input")
	_open_for_target(popup, 2, Vector2(120.0, 120.0))
	_check(popup.visible, "a real target id still opens the panel")
	await _free_world(world)


func _marker_follows_attack_acceptance() -> void:
	# The marker belongs to the click that was honoured. An accepted Attack replaces
	# the walk, so the marker goes; a confirmation C++ refused must leave the marker
	# of the walk still under way exactly where it was. Break either catch: the
	# hide-the-marker line moved outside the accepted branch, or dropped from it.
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	var ally: Node = world.get_node("Friendly")
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var popup := _attack_popup_of(world)
	if popup == null:
		await _free_world(world)
		return

	var floor_point := _floor_screen(world, camera, player.global_position, popup)
	_check(floor_point != Vector2.INF, "a floor point is reachable before the attack")
	if floor_point != Vector2.INF:
		_click_at(floor_point)
		_check(marker.visible, "the floor click raised the marker")

	_click_at(_dummy_screen(camera, enemy))
	_check(popup.visible, "the Enemy click opened its popup")
	await _press_attack(popup)
	_check(_attack_target_id(player.sheet) == 3, "that Attack was accepted")
	_check(not marker.visible, "and an accepted attack cleared the marker")

	# Now the refused half: a target that died while its popup was open.
	player.sheet.clear_order()
	if floor_point != Vector2.INF:
		_click_at(floor_point)
		_check(marker.visible, "a fresh floor click raised the marker again")
	_click_at(_dummy_screen(camera, ally))
	_check(popup.visible, "the Friendly click opened its popup")
	_check(ally.sheet.apply_damage(1, 500.0), "the pending target dies before confirmation")
	await _press_attack(popup)
	_check(_attack_target_id(player.sheet) == 0, "that confirmation was refused")
	_check(marker.visible, "and a refused attack left the marker up")
	await _free_world(world)


func _every_actor_uses_the_coordinator() -> void:
	# The plan's ownership rule: the scene registers all three configured actors and
	# hands each the same coordinator, so the main scene always takes the combat
	# path. set_position only succeeds for a registered id, which makes it a
	# registration probe: an add_character that failed and was skipped would leave
	# this check red rather than freezing a body at zero velocity.
	var world := await _new_world()
	var combat = _combat_of(world)
	if combat == null:
		await _free_world(world)
		return
	for actor in [world.get_node("Player"), world.get_node("Friendly"), world.get_node("Enemy")]:
		_check(actor.get("combat") == combat, "%s holds the world's own coordinator" % actor.name)
		var sheet: OpenCharacter = actor.get("sheet")
		_check(sheet != null, "%s still has a sheet" % actor.name)
		if sheet == null:
			continue
		var id: int = sheet.get_character_id()
		_check([1, 2, 3].has(id), "%s keeps its spike id %d" % [actor.name, id])
		_check(combat.set_position(id, actor.global_position), "%s is registered as a combatant" % actor.name)
	_check(not combat.set_position(4, Vector3.ZERO), "an id the scene never spawned is not a combatant")
	await _free_world(world)


# --- the action chat ---------------------------------------------------------------

func _chat_of(world) -> Control:
	var found: Array = world.find_children("CombatLog", "", true, false)
	_check(found.size() == 1, "the world carries exactly one action chat")
	if found.size() != 1:
		return null
	var chat = found[0]
	_check(chat is Control, "the chat is a hud control, not a body in the 3D world")
	for method in ["bind_actors", "append_swing"]:
		_check(chat.has_method(method), "and the world could wire %s()" % method)
	return chat as Control


func _chat_lines(chat) -> PackedStringArray:
	var found := PackedStringArray()
	for label in chat.find_children("*", "Label", true, false):
		found.append(String(label.text))
	return found


# The panel is drawn over floor the player clicks on, so a click that lands on it has to arrive
# downstairs. This is the real pick rather than a `mouse_filter` inspection: the world's own ray
# is asked whether that point is ground, and only then is the click pressed there.
func _a_click_on_the_chat_still_walks_the_player() -> void:
	var world := await _new_world()
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var combat = _combat_of(world)
	var chat := _chat_of(world)
	if camera == null or marker == null or combat == null or chat == null:
		await _free_world(world)
		return
	var rect: Rect2 = chat.get_global_rect()
	_check(rect.size.x > 0.0 and rect.size.y > 0.0, "the chat has a panel on screen: %s" % str(rect))
	var point := Vector2.INF
	for attempt in range(9):
		var candidate := Vector2(rect.position.x + rect.size.x * (0.15 + 0.08 * float(attempt)),
				rect.position.y + rect.size.y * (0.3 + 0.2 * float(attempt % 3)))
		if not rect.has_point(candidate):
			continue
		if _nearest_is_floor(world, camera, candidate):
			point = candidate
			break
	_check(point != Vector2.INF, "a point inside the chat panel is a floor point: %s" % str(point))
	if point == Vector2.INF:
		await _free_world(world)
		return
	_check(not bool(world.call("_popup_covers", point)),
			"the chat is not one of the panels that swallow a click")
	var walked: Vector3 = world.get_node("Player").global_position
	_click_at(point)
	_check(bool(marker.visible), "a click on top of the chat still showed the movement marker")
	for frame in range(40):
		await physics_frame
	_check(world.get_node("Player").global_position.distance_to(walked) > 0.5,
			"and the player really walked to where the chat is drawn")
	await _free_world(world)


# The chat has to hear the world's first swing through the scene's own wiring, on the frame the
# world performed it - not through a listener this test attached, and not by reading the queue the
# feedback node drains.
func _the_hud_chat_hears_the_worlds_first_swing() -> void:
	var world := await _new_world()
	var combat = _combat_of(world)
	var chat := _chat_of(world)
	var feedbacks: Array = world.find_children("CombatFeedback", "", true, false)
	_check(feedbacks.size() == 1, "the world drains the fight through one feedback node")
	if combat == null or chat == null or feedbacks.size() != 1:
		await _free_world(world)
		return
	var feedback = feedbacks[0]
	_check(feedback.get_signal_connection_list("swing_reported").size() == 1,
			"and the chat is connected to that single drain exactly once, got %d"
			% feedback.get_signal_connection_list("swing_reported").size())
	_check(_chat_lines(chat).is_empty(), "a fresh world has nothing to report yet")

	var camera: Camera3D = world.get("_camera")
	_click_at(_dummy_screen(camera, world.get_node("Enemy")))
	var popup := _attack_popup_of(world)
	if popup == null:
		await _free_world(world)
		return
	_check(popup.visible, "clicking the Enemy opened its action")
	await _press_attack(popup)
	var guard := 0
	while guard < 240 and _chat_lines(chat).is_empty():
		await physics_frame
		guard += 1
	var lines := _chat_lines(chat)
	_check(guard < 240 and lines.size() >= 1,
			"the first performed swing reached the chat through the scene, got %s" % str(lines))
	_check(combat.take_combat_events().is_empty(),
			"and reading the chat never emptied the queue the feedback owns")
	if lines.size() >= 1:
		_check(String(lines[0]).contains(" attacks"),
				"and the line is a sentence about a swing: %s" % String(lines[0]))
	await _free_world(world)


# The chat is a report, not a window: it must never be drawn over a panel the player opened, and
# it grows downward-facing into exactly the corner the popups sit in. Inside one CanvasLayer the
# later sibling is the one on top, so the chat has to be the earlier one.
func _the_chat_is_drawn_under_the_popups() -> void:
	var world := await _new_world()
	var chat := _chat_of(world)
	if chat == null:
		await _free_world(world)
		return
	var layer: Node = chat.get_parent()
	var siblings: Array = layer.get_children()
	var chat_index := siblings.find(chat)
	_check(chat_index >= 0, "the chat lives on the hud layer with the panels")
	for panel_name in ["CharacterPopup", "EquipmentPopup", "TechniquePopup", "WorkbenchPopup",
			"WeaponPopup", "AttackPopup"]:
		var found: Array = world.find_children(panel_name, "", true, false)
		if found.size() != 1:
			continue
		var popup := found[0] as Control
		if popup.get_parent() != layer:
			continue
		_check(siblings.find(chat) < siblings.find(popup),
				"the chat is behind %s, not over it: %d against %d" % [panel_name, chat_index,
						siblings.find(popup)])
	await _free_world(world)


func _the_power_key_lifts_the_player() -> void:
	# P is the playtest lever, and it lifts exactly one sheet: the player's. The
	# C++ rule is pinned by the simulation suite; what this scene check owns is
	# that the key reaches the player's sheet and nobody else's.
	var world := await _new_world()
	var player: Node = world.get_node("Player")
	var enemy: Node = world.get_node("Enemy")
	_check(player.sheet.get_strength() < 2.0, "the player starts as fresh as the sheet allows")
	_press_key(KEY_P)
	_check(player.sheet.get_strength() >= 10.0,
			"P lifts the player's strength: got %s" % str(player.sheet.get_strength()))
	var one_handed := 0.0
	for row in player.sheet.get_skill_snapshot():
		if String(row.name) == "One-Handed Weapons":
			one_handed = float(row.value)
	_check(one_handed > 100.0,
			"and the One-Handed job reports its trained hundred: got %s" % str(one_handed))
	_check(enemy.sheet.get_strength() < 2.0, "the Enemy keeps the base it spawned with")
	await _free_world(world)


func _run() -> void:
	# Read the viewport the tests actually run in, so a project window-size change
	# moves the edge cases with it instead of stranding them off screen. In headless
	# the Window's own size is a placeholder 100x100; get_visible_rect() is the
	# viewport that GUI layout and ray projection actually happen against.
	_viewport_size = root.get_visible_rect().size
	_check(_viewport_size.x > 0.0 and _viewport_size.y > 0.0, "the test viewport has a size")

	await _living_dummy_click_opens_attack()
	await _attack_button_is_not_floor_input()
	await _dismiss_and_stale_target()
	await _popup_ignores_a_non_positive_target_id()
	await _approach_then_auto_swing()
	await _friendly_target_proves_no_faction_gate()
	await _two_attackers_land_whole_blows()
	await _marker_follows_attack_acceptance()
	await _every_actor_uses_the_coordinator()
	await _scene_uses_combat_regen_once()
	await _a_click_on_the_chat_still_walks_the_player()
	await _the_chat_is_drawn_under_the_popups()
	await _the_hud_chat_hears_the_worlds_first_swing()
	await _the_power_key_lifts_the_player()

	if failures == 0:
		print("Melee combat: all checks passed")
	quit(1 if failures else 0)
