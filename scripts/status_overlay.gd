# The status overlay: one per scene, the only owner of status controls.
#
# It binds a player, a world roster, and the camera, then refreshes every view once per drawn
# frame after the camera has followed (`process_priority -100` — the main scene ticks at 0), so
# what the plaque and the overheads mirror is the frame the player is actually looking at, not
# yesterday's projection. Ownership is keyed by actor *instance*, never by numeric id alone: a
# freed actor's view is released, and a fresh node reusing an old id reads fresh identity.
# Binding the same roster twice is idempotent, unbinding is safe to repeat, and everything the
# overlay builds dies with the scene — no singleton holds status state.
#
# Gating follows the human's G2 decisions (2026-11-01): distance is the true 3D line from the
# camera to the actor's feet, visible while ≤ 50 inclusive, hidden behind the camera, and no
# screen-edge rule. Wall coverage is deliberately not part of the gate yet: under the human's
# 2026-11-01 "ship now, cover later" decision the overheads ship without a cover rule, and the
# locked "any part seen / hide only when completely covered" target waits for a ratified
# implementation (spike evidence in the plan) — never a silent approximation.

extends Control

const PlayerStatusScript := preload("res://scripts/player_status.gd")
const WorldStatusScript := preload("res://scripts/world_status.gd")

# The locked presentation constants, kept single and easy to revisit after playtesting.
const MAX_VIEW_DISTANCE := 50.0
const HEAD_ANCHOR_Y := 2.3

var _player_actor: CharacterBody3D
var _camera: Camera3D
var _player_view: Control
var _entries: Dictionary = {} # actor instance id -> {actor: CharacterBody3D, view: Control}


func _init() -> void:
	name = "StatusOverlay"
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_preset(Control.PRESET_FULL_RECT)
	process_priority = -100


# Replace the whole bound scene. `player` gets the plaque; every other valid actor gets one
# overhead. Invalid entries (freed, unconfigured, or the player) are simply never owned.
func bind_scene(player: CharacterBody3D, actors: Array, camera: Camera3D) -> void:
	_player_actor = player
	_camera = camera
	if _player_actor != null and is_instance_valid(_player_actor) and _player_view == null:
		_player_view = PlayerStatusScript.new()
		_player_view.name = "PlayerStatus"
		add_child(_player_view)
	var keep: Dictionary = {}
	for entry in actors:
		var actor := entry as CharacterBody3D
		if actor == null or not is_instance_valid(actor) or actor == _player_actor:
			continue
		var sheet: OpenCharacter = actor.get("sheet")
		if sheet == null or not is_instance_valid(sheet) or not sheet.is_configured():
			continue
		var actor_id := actor.get_instance_id()
		keep[actor_id] = true
		if not _entries.has(actor_id):
			var view: Control = WorldStatusScript.new()
			view.name = "WorldStatus_%d" % int(sheet.get_character_id())
			add_child(view)
			_entries[actor_id] = {"actor": actor, "view": view}
		else:
			(_entries[actor_id] as Dictionary)["actor"] = actor
		((_entries[actor_id] as Dictionary)["view"] as Control).bind_actor(actor)
	for actor_id in _entries.keys():
		if not keep.has(actor_id):
			var retired: Dictionary = _entries[actor_id]
			(retired["view"] as Control).free()
			_entries.erase(actor_id)


# One frame for every view: the player's plaque, then each overhead with the locked gate.
func refresh() -> void:
	if _player_view != null:
		if _player_actor != null and is_instance_valid(_player_actor):
			var sheet: OpenCharacter = _player_actor.get("sheet")
			_player_view.bind_sheet(sheet, String(_player_actor.name))
		else:
			_player_view.bind_sheet(null)
		_player_view.refresh()
	var show_any := _camera != null and is_instance_valid(_camera) and _camera.is_current()
	for actor_id in _entries.keys():
		var entry: Dictionary = _entries[actor_id]
		var view: Control = entry["view"]
		view.refresh()
		var actor: CharacterBody3D = entry["actor"]
		var show := show_any and _passes_view_gate(actor)
		view.visible = show
		if show:
			var anchor := head_anchor(entry["actor"] as CharacterBody3D)
			view.position = anchor - Vector2(view.size.x / 2.0, view.size.y)


# The production eligibility answer, public so tests (and the future cover pass) can ask the
# same question the overlay acts on. The wall-coverage half of the locked rule is deliberately
# absent under the human's 2026-11-01 "ship now, cover later" decision: range and facing are
# the whole gate until the exact contract is ratified.
func status_visible(actor: CharacterBody3D) -> bool:
	if actor == null or not is_instance_valid(actor):
		return false
	var entry: Dictionary = _entries.get(actor.get_instance_id(), {})
	if entry.is_empty():
		return false
	return bool((entry["view"] as Control).visible)


# G2, decided by the human 2026-11-01: feet origin, true 3D, ≤ 50 inclusive, and a character
# behind the camera never shows.
func _passes_view_gate(actor: CharacterBody3D) -> bool:
	if actor == null or not is_instance_valid(actor):
		return false
	var feet := actor.global_position
	if _camera.global_position.distance_to(feet) > MAX_VIEW_DISTANCE:
		return false
	return _camera.to_local(feet).z < 0.0


# The screen point a view hangs from above this actor: the same projection and the same head
# height the plates already use. Public so a display that stacks over a plate - the answer bubble
# - asks this one rule instead of writing a second one that can quietly disagree with it. A
# camera that is not drawing gets the point it would have used: the caller's own gate decides
# whether anything is shown at all.
func head_anchor(actor: CharacterBody3D) -> Vector2:
	var feet := actor.global_position + Vector3(0.0, HEAD_ANCHOR_Y, 0.0)
	if _camera == null or not is_instance_valid(_camera) or not _camera.is_current():
		return _camera.unproject_position(feet) if _camera != null else Vector2.ZERO
	return _camera.unproject_position(feet)


func _process(_delta: float) -> void:
	refresh()


# Release every child and reference. Safe to call repeatedly; a later bind_scene builds afresh.
func unbind_scene() -> void:
	_player_actor = null
	_camera = null
	if _player_view != null:
		_player_view.free()
		_player_view = null
	for actor_id in _entries.keys():
		var entry: Dictionary = _entries[actor_id]
		(entry["view"] as Control).free()
	_entries.clear()
