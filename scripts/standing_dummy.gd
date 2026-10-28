class_name StandingDummy
extends CharacterBody3D

const GRAVITY := 24.0

@export var model_scene: PackedScene

var sheet: OpenCharacter

func setup(faction: int, level: int) -> void:
	collision_layer = 2
	collision_mask = 1
	sheet = OpenCharacter.new()
	sheet.configure(faction, level)
	sheet.set_can_move(false)
	_build_visual()
	_build_label()


func _physics_process(delta: float) -> void:
	if not is_on_floor():
		velocity.y -= GRAVITY * delta
	else:
		velocity.y = 0.0
	var ordered: Vector3 = sheet.velocity_at(global_position, 4.5)
	velocity.x = ordered.x
	velocity.z = ordered.z
	move_and_slide()


func _build_label() -> void:
	var side := "Friendly" if sheet.get_faction() == 0 else "Enemy"
	var label := Label3D.new()
	label.text = "%s  Lv%d  HP %.0f  Str %.0f" % [side, sheet.get_level(), sheet.get_health(), sheet.get_strength()]
	label.position = Vector3(0.0, 2.15, 0.0)
	label.font_size = 42
	label.pixel_size = 0.012
	label.billboard = BaseMaterial3D.BILLBOARD_ENABLED
	label.modulate = Color(0.35, 0.85, 0.42) if sheet.get_faction() == 0 else Color(0.92, 0.32, 0.28)
	add_child(label)


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
