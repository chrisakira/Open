class_name Player
extends CharacterBody3D

const GRAVITY := 24.0
const TURN_SPEED := 10.0
@export var model_scene: PackedScene
@export var character_id: int = 1
@export var character_name: String = ""
@export var blessing: int = 0

var sheet: OpenCharacter
var _model: Node


func _ready() -> void:
	collision_layer = 2
	collision_mask = 1
	sheet = OpenCharacter.new()
	if not sheet.configure(character_id, character_name, 0, 1, blessing):
		push_error("Player %d could not configure its character sheet" % character_id)
	_build_visual()


func go_to(world_position: Vector3) -> void:
	if sheet == null:
		return
	sheet.set_destination(world_position)


func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	else:
		velocity.y = 0.0

	if sheet == null:
		return

	# One regen clock: the physics tick forwards time to the sheet, which owns the
	# rates. Nothing else ticks regeneration.
	sheet.regenerate(delta, false)

	var ordered: Vector3 = sheet.velocity_at(global_position)
	velocity.x = ordered.x
	velocity.z = ordered.z
	var moving := Vector2(velocity.x, velocity.z).length() > 0.05
	if moving:
		_face_direction(Vector3(ordered.x, 0.0, ordered.z).normalized(), delta)

	move_and_slide()
	if _model and _model.has_method("update_walk"):
		var horizontal_velocity := Vector2(get_real_velocity().x, get_real_velocity().z)
		var reference := sheet.get_walk_speed()
		var stride := 0.0 if reference <= 0.0 else horizontal_velocity.length() / reference
		_model.update_walk(delta, stride)


func _face_direction(direction: Vector3, delta: float) -> void:
	var target := transform.looking_at(global_position + direction, Vector3.UP)
	var from := basis.get_rotation_quaternion()
	var to := target.basis.get_rotation_quaternion()
	basis = Basis(from.slerp(to, clampf(TURN_SPEED * delta, 0.0, 1.0)))


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
		_model = model_scene.instantiate()
		visual.add_child(_model)
