class_name StandingDummy
extends CharacterBody3D

const GRAVITY := 24.0

@export var model_scene: PackedScene

var sheet: OpenCharacter
# Set by the world's combat coordinator. While it is present, that coordinator
# owns this frame's clocks, combat state and regeneration.
var combat: OpenCombat


func setup(faction: int, level: int, id: int, seed: int, name: String = "") -> void:
	collision_layer = 2
	collision_mask = 1
	sheet = OpenCharacter.new()
	if not sheet.configure_npc(id, name, faction, level, seed):
		push_error("StandingDummy %d could not configure its character sheet" % id)
	sheet.set_can_move(false)
	_build_visual()


func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	else:
		velocity.y = 0.0

	if sheet == null:
		return

	var ordered: Vector3
	if combat != null:
		# Standing still is C++'s answer, not a scene rule: this dummy has no
		# order of its own, and it never ticks clocks or regeneration.
		ordered = combat.velocity_at(sheet.get_character_id())
	else:
		# A fixture with no coordinator: one regen clock, the sheet's own.
		sheet.regenerate(delta, false)
		ordered = sheet.velocity_at(global_position)

	velocity.x = ordered.x
	velocity.z = ordered.z
	move_and_slide()


func _build_visual() -> void:
	var capsule := CollisionShape3D.new()
	var shape := CapsuleShape3D.new()
	shape.radius = 0.32
	shape.height = 1.72
	capsule.shape = shape
	capsule.position.y = 0.86
	add_child(capsule)

	var visual := Node3D.new()
	visual.name = "Visual"
	add_child(visual)

	if model_scene:
		visual.add_child(model_scene.instantiate())
