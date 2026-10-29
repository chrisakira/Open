extends Node3D

const CAMERA_OFFSET := Vector3(0.0, 9.0, 12.0)
const CAMERA_LOOK_HEIGHT := 1.2
const CAMERA_FOLLOW := 6.0
const FLOOR_SIZE := 40.0
const MALE_MODEL := preload("res://scenes/characters/male.tscn")
const FEMALE_MODEL := preload("res://scenes/characters/female.tscn")

# The spike picks the player's blessing by hand. Character creation will supply it
# later; no selection UI exists yet.
@export var player_blessing: int = 0

var _player: Player
var _camera: Camera3D
var _marker: MeshInstance3D
var _marker_tween: Tween


func _ready() -> void:
	_build_world()
	_build_player()
	_build_standing_dummies()
	_build_camera()
	_build_marker()
	_build_hud()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			_try_move_to_click(mouse.position)


func _process(delta: float) -> void:
	if _player == null or _camera == null:
		return
	var target := _player.global_position + CAMERA_OFFSET
	_camera.global_position = _camera.global_position.lerp(target, 1.0 - exp(-CAMERA_FOLLOW * delta))
	_camera.look_at(_player.global_position + Vector3(0.0, CAMERA_LOOK_HEIGHT, 0.0), Vector3.UP)


func _try_move_to_click(screen_pos: Vector2) -> void:
	var from := _camera.project_ray_origin(screen_pos)
	var to := from + _camera.project_ray_normal(screen_pos) * 1000.0
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.collision_mask = 1
	query.collide_with_areas = false
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return
	var point: Vector3 = hit.position
	_player.go_to(point)
	_show_marker(point)


func _show_marker(point: Vector3) -> void:
	_marker.global_position = point + Vector3(0.0, 0.04, 0.0)
	_marker.visible = true
	_marker.scale = Vector3.ONE
	if _marker_tween:
		_marker_tween.kill()
	_marker_tween = create_tween()
	_marker_tween.tween_property(_marker, "scale", Vector3(1.35, 1.0, 1.35), 0.18)


func _build_world() -> void:
	var env_node := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.55, 0.66, 0.74)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.62, 0.64, 0.68)
	env.ambient_light_energy = 0.45
	env.tonemap_mode = Environment.TONE_MAPPER_ACES
	env_node.environment = env
	add_child(env_node)

	var sun := DirectionalLight3D.new()
	sun.rotation_degrees = Vector3(-50.0, 40.0, 0.0)
	sun.shadow_enabled = true
	sun.light_energy = 1.15
	add_child(sun)

	var floor_body := StaticBody3D.new()
	floor_body.name = "Floor"
	floor_body.collision_layer = 1
	floor_body.collision_mask = 0
	add_child(floor_body)

	var floor_mesh := MeshInstance3D.new()
	var plane := PlaneMesh.new()
	plane.size = Vector2(FLOOR_SIZE, FLOOR_SIZE)
	floor_mesh.mesh = plane
	var floor_mat := ShaderMaterial.new()
	floor_mat.shader = _grid_shader()
	floor_mesh.material_override = floor_mat
	floor_body.add_child(floor_mesh)

	var floor_collision := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = Vector3(FLOOR_SIZE, 0.2, FLOOR_SIZE)
	floor_collision.shape = box
	floor_collision.position.y = -0.1
	floor_body.add_child(floor_collision)

	_add_block(Vector3(6.0, 0.6, -4.0), Vector3(1.4, 1.2, 1.4), Color(0.45, 0.38, 0.32))
	_add_block(Vector3(-7.0, 0.4, 5.0), Vector3(2.2, 0.8, 1.1), Color(0.36, 0.42, 0.34))
	_add_block(Vector3(3.5, 0.9, 8.0), Vector3(0.9, 1.8, 0.9), Color(0.50, 0.28, 0.22))
	_add_block(Vector3(-4.0, 0.5, -8.0), Vector3(1.6, 1.0, 1.6), Color(0.30, 0.33, 0.40))


func _add_block(pos: Vector3, size: Vector3, color: Color) -> void:
	var body := StaticBody3D.new()
	body.position = pos
	body.collision_layer = 1
	body.collision_mask = 0
	add_child(body)

	var mi := MeshInstance3D.new()
	var mesh := BoxMesh.new()
	mesh.size = size
	mi.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.roughness = 0.8
	mi.material_override = mat
	body.add_child(mi)

	var col := CollisionShape3D.new()
	var shape := BoxShape3D.new()
	shape.size = size
	col.shape = shape
	body.add_child(col)


func _build_player() -> void:
	_player = Player.new()
	_player.name = "Player"
	_player.model_scene = MALE_MODEL
	_player.character_id = 1
	_player.blessing = player_blessing
	_player.position = Vector3(0.0, 0.1, 0.0)
	add_child(_player)


func _build_standing_dummies() -> void:
	var friend := StandingDummy.new()
	friend.name = "Friendly"
	friend.model_scene = FEMALE_MODEL
	friend.position = Vector3(-6.0, 0.1, -2.0)
	friend.setup(0, 1, 2, 2002)
	add_child(friend)

	var enemy := StandingDummy.new()
	enemy.name = "Enemy"
	enemy.model_scene = FEMALE_MODEL
	enemy.position = Vector3(6.0, 0.1, -2.0)
	enemy.setup(1, 1, 3, 3003)
	add_child(enemy)


func _build_camera() -> void:
	_camera = Camera3D.new()
	_camera.name = "Camera"
	_camera.fov = 55.0
	_camera.current = true
	_camera.position = CAMERA_OFFSET
	add_child(_camera)
	_camera.look_at(Vector3(0.0, CAMERA_LOOK_HEIGHT, 0.0), Vector3.UP)


func _build_marker() -> void:
	_marker = MeshInstance3D.new()
	_marker.name = "ClickMarker"
	var ring := TorusMesh.new()
	ring.inner_radius = 0.28
	ring.outer_radius = 0.42
	ring.rings = 12
	ring.ring_segments = 24
	_marker.mesh = ring
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color(0.95, 0.82, 0.28)
	mat.emission_enabled = true
	mat.emission = Color(0.95, 0.75, 0.15)
	mat.emission_energy_multiplier = 1.4
	_marker.material_override = mat
	_marker.visible = false
	add_child(_marker)


func _build_hud() -> void:
	var layer := CanvasLayer.new()
	add_child(layer)
	var label := Label.new()
	label.text = "Left click the floor to move"
	label.position = Vector2(24, 20)
	label.add_theme_font_size_override("font_size", 22)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(label)


func _grid_shader() -> Shader:
	var shader := Shader.new()
	shader.code = """
shader_type spatial;
render_mode cull_disabled, specular_disabled;

void fragment() {
	vec2 world = UV * 40.0;
	vec2 cell = abs(fract(world) - 0.5);
	float line = 1.0 - smoothstep(0.02, 0.035, min(cell.x, cell.y));
	vec3 base = vec3(0.22, 0.24, 0.20);
	vec3 grid = vec3(0.32, 0.34, 0.28);
	ALBEDO = mix(base, grid, line);
	ROUGHNESS = 0.9;
}
"""
	return shader
