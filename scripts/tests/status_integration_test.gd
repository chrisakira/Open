extends SceneTree

# Task 4 of the status UID plan: one scene-owned overlay, the player wired into it.
#
# The overlay is the only thing that may own status controls, exactly one of it per scene, and
# it refreshes after the camera so the plaque and the overheads mirror a frame the player is
# actually looking at. The real main scene is instantiated for real here: layer, sibling order,
# automatic refresh and scene reentry are asserted where they happen, not in a mock. World
# entries are exercised only in isolated fixtures — production attaches an empty roster until
# the G1 cover gate clears (plan Task 4), and these tests hold that boundary too.
#
# The distance filter follows the human's G2 decisions from 2026-11-01 — camera to feet origin,
# true 3D, 50 inclusive, hidden behind the camera — so fixtures stand dummies at 49, 50 and 51
# and behind the lens, and the answers must match.

var failures := 0
var checks := 0

const OVERLAY_PATH := "res://scripts/status_overlay.gd"
const WORLD_PATH := "res://scripts/world_status.gd"



func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


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


func _world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	return world


# --- cases ---------------------------------------------------------------------

func _the_scene_carries_one_overlay_bound_to_the_player() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	_check(overlay != null, "the scene carries a StatusOverlay")
	if overlay == null:
		return
	_check(_count(world, "StatusOverlay") == 1, "exactly one overlay per scene")
	var player_status := _find(overlay, "PlayerStatus")
	_check(player_status != null, "the overlay owns the PlayerStatus")
	_check(player_status != null and player_status.get_child_count() > 0,
			"the player view is built")
	var name_label := _find(player_status, "CharacterName") as Label
	_check(name_label != null and name_label.text == "John Doe",
			"the plaque is bound to the player's real sheet name")
	var layer := overlay.get_parent()
	_check(layer is CanvasLayer, "the overlay lives on the HUD canvas layer")
	var log := _find(world, "CombatLog")
	_check(log != null and log.get_index() < overlay.get_index(),
			"the overlay sits above the chat in draw order")
	var popups_above := 0
	var popups_seen := 0
	for popup_name in ["CharacterPopup", "EquipmentPopup", "TechniquePopup", "WorkbenchPopup",
			"AttackPopup", "WeaponPopup"]:
		var popup := _find(world, popup_name)
		if popup != null:
			popups_seen += 1
			if popup.get_parent() == layer and popup.get_index() > overlay.get_index():
				popups_above += 1
	_check(popups_seen >= 5 and popups_above == popups_seen,
			"every popup shares the layer and stays above the status (saw %d)" % popups_seen)
	_check(int(overlay.process_priority) < 0,
			"the overlay refreshes after the camera: priority %d" % int(overlay.process_priority))
	world.queue_free()
	await process_frame
	await process_frame


func _the_player_view_follows_damage_with_no_manual_refresh() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	var player := _find(world, "Player")
	if overlay == null or player == null:
		_check(false, "the wired scene had an overlay and a player")
		return
	var name_label := _find(overlay, "CharacterName") as Label
	_check(name_label != null, "the player view is showing")
	_check(player.sheet.apply_damage(2, 23.0), "the player really takes damage")
	await process_frame
	await process_frame
	var hp_row := _find(overlay, "HP")
	var value: Label = null
	if hp_row != null:
		value = _find(hp_row, "Value") as Label
	_check(value != null and value.text == "77.00 / 100.00",
			"the plaque followed the pool with no hand-refreshed seam")
	_check(bool(player.sheet.is_alive()), "and the sim agrees it still lives")
	world.queue_free()
	await process_frame
	await process_frame


func _reentry_leaves_no_ghosts() -> void:
	var first := _world()
	await process_frame
	await physics_frame
	first.queue_free()
	await process_frame
	await process_frame
	_check(_count(root, "StatusOverlay") == 0, "a freed world frees its overlay with it")
	var second := _world()
	await process_frame
	await physics_frame
	await process_frame
	_check(_count(second, "StatusOverlay") == 1, "a fresh world builds exactly one overlay")
	var overlay := _find(second, "StatusOverlay")
	var name_label := _find(overlay, "CharacterName") as Label
	_check(name_label != null and name_label.text == "John Doe",
			"and it mirrors the new world's player, not a stale one")
	second.queue_free()
	await process_frame
	await process_frame


func _world_entries_are_owned_per_actor_in_fixtures() -> void:
	var script: Script = load(OVERLAY_PATH)
	if script == null:
		_check(false, "the overlay exists at %s" % OVERLAY_PATH)
		return
	var stage := SubViewport.new()
	stage.size = Vector2i(1280, 720)
	root.add_child(stage)
	var camera := Camera3D.new()
	stage.add_child(camera)
	camera.current = true
	# Identity camera at the origin looking down -Z: fixtures stand actors at exact distances
	# and exact boundaries without any trigonometry.
	camera.position = Vector3.ZERO
	var layer := CanvasLayer.new()
	stage.add_child(layer)
	var overlay: Control = script.new()
	layer.add_child(overlay)
	await process_frame

	var hero := _dummy(0, 1, 501, 77, "Hero", Vector3(0.0, 0.0, 0.0))
	var ally := _dummy(0, 1, 502, 78, "Ally", Vector3(-1.0, 0.0, 0.0))
	var foe := _dummy(1, 1, 503, 79, "Foe", Vector3(-2.0, 0.0, 0.0))
	stage.add_child(hero)
	stage.add_child(ally)
	stage.add_child(foe)
	await process_frame

	overlay.bind_scene(hero, [ally, foe], camera)
	overlay.refresh()
	_check(_count(overlay, "PlayerStatus") == 1, "the bound scene has one player view")
	var ally_view := _find(overlay, "WorldStatus_502")
	var foe_view := _find(overlay, "WorldStatus_503")
	_check(ally_view != null and foe_view != null, "each roster actor has its named view")
	_check(_find(overlay, "WorldStatus_501") == null, "the player never gets an overhead")
	# Binding the same roster twice is idempotent: stable counts, no duplicates.
	overlay.bind_scene(hero, [ally, foe], camera)
	overlay.refresh()
	_check(_count(overlay, "WorldStatus_502") == 1 and _count(overlay, "WorldStatus_503") == 1,
			"re-binding the same roster keeps exactly one view per actor")
	_check(_count(overlay, "PlayerStatus") == 1, "and one player view")

	# G2 boundaries (feet origin, true 3D, ≤ 50 inclusive): 49 shown, exactly 50 shown,
	# 51 hidden — and the vertical mix that only true 3D passes.
	var near := _dummy(0, 1, 504, 80, "Near", Vector3(0.0, 0.0, 0.0))
	stage.add_child(near)
	await process_frame
	overlay.bind_scene(hero, [near], camera)
	var near_view := _find(overlay, "WorldStatus_504")
	near.global_position = Vector3(0.0, 0.0, -49.0)
	overlay.refresh()
	_check(near_view != null and (near_view as CanvasItem).visible, "49 units away is shown")
	near.global_position = Vector3(0.0, 0.0, -50.0)
	overlay.refresh()
	_check((near_view as CanvasItem).visible, "exactly 50 still counts")
	near.global_position = Vector3(0.0, 0.0, -51.0)
	overlay.refresh()
	_check(not (near_view as CanvasItem).visible, "51 is out of range and hidden")
	near.global_position = Vector3(0.0, -30.0, -40.0)
	overlay.refresh()
	_check((near_view as CanvasItem).visible, "40 ahead and 30 below is exactly 50, still shown")
	near.global_position = Vector3(0.0, 0.0, 10.0)
	overlay.refresh()
	_check(not (near_view as CanvasItem).visible, "behind the camera is hidden")

	# A freed actor leaves no orphan; reusing its numeric id on a fresh node reads fresh.
	var doomed := _dummy(1, 1, 505, 81, "Doomed", Vector3(-3.0, 0.0, 0.0))
	stage.add_child(doomed)
	await process_frame
	overlay.bind_scene(hero, [doomed], camera)
	overlay.refresh()
	_check(_find(overlay, "WorldStatus_505") != null, "the doomed actor gets a view")
	overlay.unbind_scene()
	overlay.unbind_scene()
	_check(_count(overlay, "WorldStatus_505") == 0 and _count(overlay, "PlayerStatus") == 0,
			"unbinding releases the children and is safe to repeat")
	doomed.queue_free()
	await process_frame
	var twin := _dummy(1, 3, 505, 82, "Twin", Vector3(-3.0, 0.0, 0.0))
	stage.add_child(twin)
	await process_frame
	overlay.bind_scene(hero, [twin], camera)
	overlay.refresh()
	var twin_view := _find(overlay, "WorldStatus_505")
	_check(twin_view != null and _find(twin_view, "CharacterName").text == "Twin",
			"a reused numeric id shows the new actor's identity, not the old")
	overlay.unbind_scene()
	stage.queue_free()
	hero.queue_free()
	ally.queue_free()
	foe.queue_free()
	near.queue_free()
	twin.queue_free()
	await process_frame
	await process_frame


# --- helpers -------------------------------------------------------------------

func _dummy(faction: int, level: int, id: int, seed: int, character_name: String,
		position: Vector3) -> StandingDummy:
	var dummy := StandingDummy.new()
	dummy.name = "Dummy" + character_name
	dummy.setup(faction, level, id, seed, character_name)
	dummy.global_position = position
	return dummy


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


func _free_world(world: Node) -> void:
	world.queue_free()
	await process_frame
	await process_frame


# --- task 6: acceptance on the real scene ----------------------------------------

var _swing_events := 0


func _on_swing(_event: Dictionary) -> void:
	_swing_events += 1


func _clicks_on_the_plaque_still_walk_the_player() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	var plaque := _find(overlay, "PlayerStatus")
	var player := _find(world, "Player")
	if overlay == null or plaque == null or player == null:
		_check(false, "the wired scene had overlay, plaque and player")
		return
	var rect: Rect2 = plaque.get_global_rect()
	_check(rect.size.x > 100.0 and rect.size.y > 50.0, "the plaque really occupies screen space")
	# The plaque centre is over the world; clicking it must be a floor click.
	_click_at(rect.get_center())
	await physics_frame
	await physics_frame
	var marker := _find(world, "ClickMarker") as VisualInstance3D
	_check(marker != null and marker.visible, "the click reached the floor and placed the marker")
	var velocity: Vector3 = player.sheet.velocity_at(player.global_position)
	_check(velocity.length() > 0.1, "and C++ really ordered the walk")
	var start: Vector3 = player.global_position
	for _i in 20:
		await physics_frame
	_check(player.global_position.distance_to(start) > 0.1, "and the player actually moved")
	await _free_world(world)


func _popups_keep_priority_and_consumption() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var popup := _find(world, "CharacterPopup") as Control
	var plaque := _find(_find(world, "StatusOverlay"), "PlayerStatus")
	var player := _find(world, "Player")
	if popup == null or plaque == null or player == null:
		_check(false, "the wired scene had popup, plaque and player")
		return
	_press_key(KEY_I)
	await process_frame
	_check(popup.visible, "the character popup opens over the status world")
	# A click inside the popup must be consumed there and must not order a walk.
	var inside := popup.get_global_rect().get_center()
	_click_at(inside)
	await physics_frame
	await physics_frame
	var velocity: Vector3 = player.sheet.velocity_at(player.global_position)
	_check(velocity.length() < 0.01, "a click on the popup sent no floor order")
	# A click on the chat area, uncovered by any popup, still reaches the world.
	var log := _find(world, "CombatLog")
	_check(log != null, "the chat is on the layer")
	if log != null:
		# The chat sits bottom-left and the popup did not move it; click its centre.
		var log_point: Vector2 = (log as Control).get_global_rect().get_center()
		var popup_covers_log := popup.get_global_rect().has_point(log_point)
		_check(not popup_covers_log, "the chosen log point is outside the open popup")
		_click_at(log_point)
		await physics_frame
		await physics_frame
		var walk: Vector3 = player.sheet.velocity_at(player.global_position)
		_check(walk.length() > 0.1, "the click on the chat area still walks the player")
	# Both status surfaces stay below the popup in the same layer.
	var overlay := _find(world, "StatusOverlay")
	_check(overlay.get_index() < popup.get_index(), "the popup is a later sibling than the overlay")
	_press_key(KEY_I)
	await process_frame
	_check(not popup.visible, "the popup closes again")
	await _free_world(world)


func _the_chat_still_owns_the_only_swing_report() -> void:
	_swing_events = 0
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var feedback = world.get("_feedback")
	var log = world.get("_log")
	var player := _find(world, "Player")
	var foe := _find(world, "Enemy")
	if feedback == null or log == null or player == null or foe == null:
		_check(false, "the wired scene had feedback, log, player and enemy")
		return
	feedback.swing_reported.connect(_on_swing)
	for _i in 20:
		await physics_frame
	var entries: Node = log.get_node_or_null("Entries")
	_check(entries != null, "the chat keeps an entries box")
	if entries != null:
		_check(entries.get_child_count() == 0,
				"twenty status-refreshed frames wrote no chat lines of their own")
	_check(_swing_events == 0, "and no phantom swing was reported")
	var combat = world.get("_combat")
	_check(combat != null, "the wired scene owns a combat coordinator")
	if combat == null:
		await _free_world(world)
		return
	_check(combat.issue_attack(int(player.sheet.get_character_id()),
			int(foe.sheet.get_character_id())), "the player is ordered to attack")
	var waited := 0
	while _swing_events < 1 and waited < 240:
		await physics_frame
		waited += 1
	_check(_swing_events >= 1, "a real swing really reported")
	# The performed swing spent stamina: the plaque must show the live number, not the
	# pre-fight one and never the enemy's.
	var plaque := _find(_find(world, "StatusOverlay"), "PlayerStatus")
	if plaque != null:
		var stamina_text := String(_find(plaque, "Stamina").get_node("Value").text)
		var shown := float(stamina_text.split(" / ")[0])
		# The plaque paints on its own frame, so compare against the getter with a
		# one-second-of-regeneration tolerance rather than demanding same-frame equality.
		_check(absf(shown - float(player.sheet.get_stamina())) < 1.0,
				"after the swing the stamina bar tracks the live getter (%s)" % stamina_text)
	if entries != null:
		_check(entries.get_child_count() == _swing_events,
				"one line per reported swing, never a second source (%d vs %d)"
				% [entries.get_child_count(), _swing_events])
	_check(_swing_events <= 2, "the fight stayed short enough to count (ran %d)"
			% _swing_events)
	await _free_world(world)


func _the_plaque_mirrors_live_combat_not_the_enemy() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	var plaque := _find(overlay, "PlayerStatus")
	var player := _find(world, "Player")
	var foe := _find(world, "Enemy")
	if plaque == null or player == null or foe == null:
		_check(false, "the wired scene had plaque, player and enemy")
		return
	var presenter: Script = load("res://scripts/status_presenter.gd")
	# Wound the enemy: nothing on the player's plaque may move.
	var before: String = _find(plaque, "HP").get_node("Value").text
	_check(foe.sheet.apply_damage(2, 30.0), "the enemy takes thirty")
	await process_frame
	await process_frame
	_check(String(_find(plaque, "HP").get_node("Value").text) == before,
			"wounding the enemy changed no digit of the player's plaque")
	# Damage the player through the real command and let the automatic frame do the rest.
	_check(player.sheet.apply_damage(2, 12.5), "the player takes twelve and a half")
	await process_frame
	await process_frame
	var hp_text := String(_find(plaque, "HP").get_node("Value").text)
	_check(hp_text == String(presenter.pool_amounts(float(player.sheet.get_health()),
			float(player.sheet.get_max_health()))),
			"the plaque shows the live getter ratio, not a stale one (%s)" % hp_text)
	var mana_text := String(_find(plaque, "Mana").get_node("Value").text)
	_check(mana_text == String(presenter.pool_amounts(float(player.sheet.get_mana()),
			float(player.sheet.get_max_mana()))),
			"and mana prints faithfully whatever C++ holds")
	# A maximum change through the existing lever must be visible next frame, unforced.
	_check(player.sheet.power_up(), "the playtest lever powers the player up")
	await process_frame
	await process_frame
	var fresh := String(_find(plaque, "HP").get_node("Value").text)
	_check(fresh.ends_with("1000.00") or float(fresh.split(" / ")[1]) > 100.0,
			"the new maximum shows with no manual refresh (%s)" % fresh)
	await _free_world(world)


func _resizing_the_real_scene_keeps_the_layout() -> void:
	var stage := SubViewport.new()
	stage.size = Vector2i(1280, 720)
	root.add_child(stage)
	var world: Node = load("res://scenes/main.tscn").instantiate()
	stage.add_child(world)
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	var plaque := _find(overlay, "PlayerStatus") as Control
	var log := _find(world, "CombatLog") as Control
	if plaque == null or log == null:
		_check(false, "the staged scene had plaque and chat")
		stage.queue_free()
		return
	var wide := plaque.get_global_rect()
	_check(absf(wide.get_center().x - 640.0) < 1.0, "bottom-centre at 1280 wide")
	_check(absf(wide.end.y - (720.0 - 16.0)) < 1.0, "and sixteen above the bottom edge")
	stage.size = Vector2i(720, 420)
	await process_frame
	await process_frame
	var small := plaque.get_global_rect()
	_check(absf(small.get_center().x - 360.0) < 1.0, "still bottom-centre at 720 wide")
	_check(absf(small.end.y - (420.0 - 16.0)) < 1.0, "still hugging the new bottom")
	_check(absf((log as Control).get_global_position().x - 16.0) < 1.0,
			"the chat keeps its locked left margin whatever the plaque does")
	_check(String(_find(plaque, "CharacterName").text) == "John Doe",
			"the squeeze never touched the numbers")
	stage.size = Vector2i(1280, 720)
	await process_frame
	await process_frame
	_check(absf(plaque.get_global_rect().get_center().x - 640.0) < 1.0,
			"and returning to 1280 returns the plaque")
	stage.queue_free()
	await process_frame
	await process_frame


func _the_scene_shows_both_dummies_overhead() -> void:
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var overlay := _find(world, "StatusOverlay")
	if overlay == null:
		_check(false, "the wired scene has an overlay")
		return
	var views: Array = world.find_children("WorldStatus_*", "", true, false)
	_check(views.size() == 2, "the two real dummies own exactly two overhead views (%d)"
			% views.size())
	var ally := _find(world, "Friendly")
	var foe := _find(world, "Enemy")
	var ally_view: Node = null
	var foe_view: Node = null
	if ally != null:
		ally_view = overlay.find_child("WorldStatus_%d" % int(ally.sheet.get_character_id()),
				true, false)
	if foe != null:
		foe_view = overlay.find_child("WorldStatus_%d" % int(foe.sheet.get_character_id()),
				true, false)
	_check(ally_view != null and foe_view != null, "one view per dummy, named by character id")
	if ally_view != null:
		var ally_name := (ally_view as Node).find_child("CharacterName", true, false) as Label
		_check(ally_name != null and ally_name.text == "Friendly",
				"the empty sheet name falls back to the scene node name")
		var ally_diamond := (ally_view as Node).find_child("RelationshipDiamond", true, false)
		_check(ally_diamond != null and ally_diamond.get("diamond_color") == Color("#4E7A4A"),
				"the ally diamond is the locked green")
		_check((ally_view as CanvasItem).visible, "the ally overhead is shown")
		_check(overlay.status_visible(ally), "and status_visible answers true")
	if foe_view != null:
		var foe_name := (foe_view as Node).find_child("CharacterName", true, false) as Label
		_check(foe_name != null and foe_name.text == "Enemy", "the enemy reads Enemy")
		var foe_diamond := (foe_view as Node).find_child("RelationshipDiamond", true, false)
		_check(foe_diamond != null and foe_diamond.get("diamond_color") == Color("#8F423D"),
				"the enemy diamond is the locked red")
	_check(not overlay.status_visible(_find(world, "Player")),
			"the player never answers as a world status")
	# The 50-unit gate applies for real: carry the gate maths by moving the ally far away.
	if ally != null:
		ally.global_position = Vector3(0.0, 0.0, -40.0)
		await process_frame
		await process_frame
		_check(not overlay.status_visible(ally), "40+ units back and the overhead goes")
		ally.global_position = Vector3(0.0, 0.0, -4.0)
		await process_frame
		await process_frame
		_check(overlay.status_visible(ally), "and returns when the actor returns")
	await _free_world(world)


func _live_overheads_answer_clicks_like_the_world_they_float_on() -> void:
	# Four fresh scenes, one clean click each, following melee_combat_test's push_input route.
	var camera: Camera3D = null

	# A: clicking a living body's own point opens the Attack popup and orders no walk.
	var world := _world()
	await process_frame
	await physics_frame
	await process_frame
	var ally := _find(world, "Friendly")
	camera = _find(world, "Camera") as Camera3D
	_click_at(camera.unproject_position(ally.global_position + Vector3(0.0, 1.0, 0.0)))
	await process_frame
	var attack_popup := _find(world, "AttackPopup") as Control
	_check(attack_popup != null and attack_popup.visible, "the body click opened the attack popup")
	_check(ally.sheet.velocity_at(ally.global_position).length() < 0.01
			and _find(world, "Player").sheet.velocity_at(
					_find(world, "Player").global_position).length() < 0.01,
			"and sent no floor order")
	await _free_world(world)

	# B: clicking inside the overhead's own rect is a click on the world behind it.
	world = _world()
	await process_frame
	await physics_frame
	await process_frame
	ally = _find(world, "Friendly")
	camera = _find(world, "Camera") as Camera3D
	var overlay := _find(world, "StatusOverlay")
	var overhead := overlay.find_child("WorldStatus_%d" % int(ally.sheet.get_character_id()),
			true, false) as Control
	_check(overhead != null and overhead.visible, "the ally overhead floats over the world")
	_click_at(overhead.get_global_rect().get_center())
	await physics_frame
	await physics_frame
	var player := _find(world, "Player")
	_check(player.sheet.velocity_at(player.global_position).length() > 0.1,
			"the click passed through the overhead and ordered a walk")
	await _free_world(world)

	# C: a corpse's own body point stays inert - no popup, no walk through to the floor.
	world = _world()
	await process_frame
	await physics_frame
	await process_frame
	var foe := _find(world, "Enemy")
	camera = _find(world, "Camera") as Camera3D
	_check(foe.sheet.apply_damage(0, 25.0), "the enemy dies for real")
	await process_frame
	_click_at(camera.unproject_position(foe.global_position + Vector3(0.0, 1.0, 0.0)))
	await process_frame
	attack_popup = _find(world, "AttackPopup") as Control
	player = _find(world, "Player")
	_check(not attack_popup.visible, "the corpse offers no popup")
	_check(player.sheet.velocity_at(player.global_position).length() < 0.01,
			"and the click does not fall through to a walk")
	# D: the dead body's overhead still floats, still visible, still click-through.
	overlay = _find(world, "StatusOverlay")
	var dead_view := overlay.find_child("WorldStatus_%d" % int(foe.sheet.get_character_id()),
			true, false) as Control
	_check(dead_view != null and dead_view.visible, "the corpse keeps its overhead")
	_click_at(dead_view.get_global_rect().get_center())
	await physics_frame
	await physics_frame
	_check(player.sheet.velocity_at(player.global_position).length() > 0.1,
			"through the dead overhead the world still answers")
	await _free_world(world)


# --- runner --------------------------------------------------------------------

func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	await _case("the scene carries one overlay bound to the player", 9,
			_the_scene_carries_one_overlay_bound_to_the_player)
	await _case("the player view follows damage with no manual refresh", 4,
			_the_player_view_follows_damage_with_no_manual_refresh)
	await _case("reentry leaves no ghosts", 3, _reentry_leaves_no_ghosts)
	await _case("world entries are owned per actor in fixtures", 13,
			_world_entries_are_owned_per_actor_in_fixtures)
	await _case("clicks on the plaque still walk the player", 4,
			_clicks_on_the_plaque_still_walk_the_player)
	await _case("popups keep priority and consumption", 7,
			_popups_keep_priority_and_consumption)
	await _case("the chat still owns the only swing report", 7,
			_the_chat_still_owns_the_only_swing_report)
	await _case("the plaque mirrors live combat, not the enemy", 7,
			_the_plaque_mirrors_live_combat_not_the_enemy)
	await _case("resizing the real scene keeps the layout", 7,
			_resizing_the_real_scene_keeps_the_layout)
	await _case("the scene shows both dummies overhead", 11,
			_the_scene_shows_both_dummies_overhead)
	await _case("live overheads answer clicks like the world they float on", 8,
			_live_overheads_answer_clicks_like_the_world_they_float_on)

	_check(checks > 92, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Status integration: all checks passed")
	quit(1 if failures else 0)
