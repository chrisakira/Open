class_name CombatFeedback
extends Node3D

# What the simulation did, shown: what each sheet is holding, the swing it performed, and the
# damage that arrived. The world's coordinator owns the fight; this node reads its reports.
#
# Nothing is decided here. Held models come from a sheet's own equipment snapshot, a motion
# plays because C++ said a swing happened, and a number is the damage that was already applied.
# That direction matters twice over: a label that also applied damage would count the blow
# twice, and a motion played for an attempt the simulation refused - a cooling clock, a target
# out of reach, a stunned sheet - would animate a swing that never happened.
#
# The reports are drained once, here, on the frame after the world's clock ran. Whatever is not
# read is kept by the coordinator rather than dropped, so a listener cannot miss a blow, and a
# listener that reads cannot be handed the same blow again.

const MODEL_PATH := "res://assets/weapons/quaternius_medieval/%s.obj"
const MODEL_SCALE := 0.22
const SLOT_LEFT_HAND := 5
const SLOT_RIGHT_HAND := 6
const SOCKET_NAMES := {SLOT_LEFT_HAND: "LeftHandSocket", SLOT_RIGHT_HAND: "RightHandSocket"}
# Where a held item sits on a body this size. The pose script animates the skeleton and this
# node follows the body the item is hung from; the exact stance is presentation, and the fact
# that the item is in the hand the simulation says it is in is not.
const SOCKET_OFFSETS := {
	"LeftHandSocket": Vector3(-0.34, 1.06, -0.16),
	"RightHandSocket": Vector3(0.34, 1.06, -0.16),
}
const SWING_ANGLE := -34.0
const SWING_SECONDS := 0.28
const LABEL_LIFETIME := 1.1
const LABEL_HEIGHT := 2.35
const MIN_FONT_SIZE := 22
const MAX_FONT_SIZE := 52

var _combat: OpenCombat
var _actors: Array = []
# One entry per actor id: the equipment state its attachments were made from. Presentation is
# rebuilt when that changes and not one frame earlier, so a held mesh cannot flicker.
var _signatures := {}
# How many attack motions have been played for each actor since it was bound. It exists because
# "one motion per performed swing, none for a refused one" is a fact worth checking and a
# tween's mid-flight angle is not a stable thing to assert. Nothing in the fight reads it.
var swing_motions := {}
var _tweens := {}
var _label_count := 0


func _ready() -> void:
	set_physics_process(false)


# The coordinator to read and the actors to draw for. Either may be empty in a fixture, and
# neither is a thing this node keeps a second copy of.
func bind_combat(combat: OpenCombat, actors: Array) -> void:
	_combat = combat
	_actors = actors
	_signatures.clear()
	set_physics_process(_combat != null)


func swing_motion_count(actor_id: int) -> int:
	return int(swing_motions.get(actor_id, 0))


func _physics_process(_delta: float) -> void:
	if _combat == null:
		return
	_report_held_items()
	_play_reports(_combat.take_combat_events())


# --- held models --------------------------------------------------------------

func _report_held_items() -> void:
	for actor in _actors:
		if not is_instance_valid(actor):
			continue
		var sheet = actor.get("sheet")
		if sheet == null or not sheet.is_configured():
			continue
		var actor_id: int = sheet.get_character_id()
		var rows: Array = sheet.get_equipment_snapshot()
		var wanted := {}
		var signature := ""
		for row in rows:
			var slots: Array = row["slots"]
			if slots.is_empty():
				continue
			var item_id := int(row["item_id"])
			var model_key := String(row["model_key"])
			# One record, one mesh: a two-handed item names both hands and is still one thing
			# in one hand.
			var slot := SLOT_RIGHT_HAND if slots.has(SLOT_RIGHT_HAND) else int(slots[0])
			wanted["%d:%s:%d" % [item_id, model_key, slot]] = {
				"item_id": item_id, "model_key": model_key, "slot": slot,
			}
		var keys := wanted.keys()
		keys.sort()
		signature = ",".join(keys)
		if String(_signatures.get(actor_id, "")) == signature:
			continue
		_signatures[actor_id] = signature
		_sync_attachments(actor, wanted)


func _sync_attachments(actor: Node3D, wanted: Dictionary) -> void:
	# What belongs on the hands is an instance in a slot, not a model: two swords from
	# one forge share a model and need a mesh each, and a sword that changes hands moves
	# its mesh with it rather than leaving it hanging where it was parked.
	var wanted_pairs := {}
	for key in wanted.keys():
		var entry: Dictionary = wanted[key]
		wanted_pairs[_holder_key(int(entry["item_id"]), int(entry["slot"]))] = entry
	for holder in _held_nodes(actor):
		var holder_key := _holder_key(int(holder.get_meta("item_id", 0)),
				int(holder.get_meta("slot", -1)))
		if not wanted_pairs.has(holder_key):
			holder.free()
	for key in wanted.keys():
		var entry: Dictionary = wanted[key]
		if _find_held(actor, int(entry["item_id"]), int(entry["slot"])) != null:
			continue
		_attach(actor, int(entry["item_id"]), String(entry["model_key"]), int(entry["slot"]))


func _holder_key(item_id: int, slot: int) -> String:
	return "%d:%d" % [item_id, slot]


func _attach(actor: Node3D, item_id: int, model_key: String, slot: int) -> void:
	var socket := _socket(actor, slot)
	if socket == null:
		return
	var mesh := load(MODEL_PATH % model_key) as Mesh
	if mesh == null:
		push_error("CombatFeedback: no model for %s" % model_key)
		return
	var held := MeshInstance3D.new()
	held.name = "Held_%s" % model_key
	held.mesh = mesh
	held.scale = Vector3.ONE * MODEL_SCALE
	# The scenery's own standing orientation: these models run along Y and a held one should
	# run along the arm instead.
	held.rotation_degrees.x = 90.0
	held.set_meta("item_id", item_id)
	held.set_meta("model_key", model_key)
	held.set_meta("slot", slot)
	socket.add_child(held)


func _socket(actor: Node3D, slot: int) -> Node3D:
	var visual := actor.get_node_or_null("Visual") as Node3D
	if visual == null:
		push_error("CombatFeedback: %s has no Visual to hang an item from" % actor.name)
		return null
	var socket_name := String(SOCKET_NAMES.get(slot, ""))
	if socket_name.is_empty():
		return null
	var found := visual.get_node_or_null(socket_name) as Node3D
	if found != null:
		return found
	var socket := Node3D.new()
	socket.name = socket_name
	socket.position = SOCKET_OFFSETS[socket_name]
	visual.add_child(socket)
	return socket


func _held_nodes(actor: Node3D) -> Array:
	return actor.find_children("Held_*", "MeshInstance3D", true, false)


func _find_held(actor: Node3D, item_id: int, slot: int) -> MeshInstance3D:
	for holder in _held_nodes(actor):
		if int(holder.get_meta("item_id", 0)) == item_id \
				and int(holder.get_meta("slot", -1)) == slot:
			return holder as MeshInstance3D
	return null


# --- swings and damage --------------------------------------------------------

func _play_reports(events: Array) -> void:
	for event in events:
		var actor := _actor_for(int(event["actor_id"]))
		if actor != null:
			_play_swing(actor, int(event["actor_id"]))
		var damage := float(event["damage"])
		if damage > 0.0:
			_show_damage(int(event["target_id"]), damage)
		# A blow that arrived as nothing - turned aside, or soaked by armour to zero - is not
		# shown as a number. It was still performed, and the swing above is its report.


func _actor_for(actor_id: int) -> Node3D:
	for actor in _actors:
		if not is_instance_valid(actor):
			continue
		var sheet = actor.get("sheet")
		if sheet != null and sheet.is_configured() and sheet.get_character_id() == actor_id:
			return actor
	return null


func _play_swing(actor: Node3D, actor_id: int) -> void:
	var visual := actor.get_node_or_null("Visual") as Node3D
	if visual == null:
		return
	swing_motions[actor_id] = swing_motion_count(actor_id) + 1
	var running = _tweens.get(actor_id)
	if running is Tween and running.is_valid():
		running.kill()
	# A lunge, not an animation: the point is that the sheet visibly did something on the frame
	# C++ says it swung, and that it returns to where it was standing.
	visual.rotation_degrees.x = 0.0
	var tween := create_tween()
	_tweens[actor_id] = tween
	tween.tween_property(visual, "rotation_degrees:x", SWING_ANGLE, SWING_SECONDS * 0.35)
	tween.tween_property(visual, "rotation_degrees:x", 0.0, SWING_SECONDS * 0.65)


func _show_damage(target_id: int, damage: float) -> void:
	var target := _actor_for(target_id)
	if target == null:
		return
	_label_count += 1
	var label := Label3D.new()
	label.name = "DamageLabel%d" % _label_count
	label.text = _damage_text(damage)
	label.position = target.global_position + Vector3(0.0, LABEL_HEIGHT, 0.0)
	label.font_size = _font_size_for(damage)
	label.pixel_size = 0.012
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.outline_size = 8
	label.modulate = Color(1.0, 0.88, 0.42, 1.0)
	add_child(label)
	var tween := create_tween()
	tween.set_parallel(true)
	tween.tween_property(label, "position:y", label.position.y + 0.6, LABEL_LIFETIME)
	tween.tween_property(label, "modulate:a", 0.0, LABEL_LIFETIME * 0.6).set_delay(LABEL_LIFETIME * 0.4)
	tween.chain().tween_callback(label.queue_free)


# Five decimals, then whatever is left: a rank-1 blow is worth under a hundredth of damage, and
# a display that rounds it to zero would be reporting that nothing happened when C++ said
# something did. A big hit keeps its whole number for the same reason.
func _damage_text(damage: float) -> String:
	var text := String.num(damage, 5)
	while text.ends_with("0") and not text.ends_with(".0"):
		text = text.substr(0, text.length() - 1)
	if text.ends_with(".0"):
		text = text.substr(0, text.length() - 1)
	if damage > 0.0 and float(text) <= 0.0:
		# Five fixed decimals cannot hold this small a positive, and "0" would report
		# that nothing arrived when C++ says something did. The engine's own default
		# conversion does not round away a positive: it just gets longer about it.
		text = String.num(damage)
	return text


# Bigger hurts bigger, and both extremes stay on the screen: a chip is readable and a
# quality-tier-seven blow does not fill the viewport.
func _font_size_for(damage: float) -> int:
	return clampi(int(MIN_FONT_SIZE + sqrt(maxf(damage, 0.0)) * 7.0), MIN_FONT_SIZE, MAX_FONT_SIZE)
