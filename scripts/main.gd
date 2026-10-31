extends Node3D

const CAMERA_OFFSET := Vector3(0.0, 9.0, 12.0)
const CAMERA_LOOK_HEIGHT := 1.2
const CAMERA_FOLLOW := 6.0
const FLOOR_SIZE := 40.0
const MALE_MODEL := preload("res://scenes/characters/male.tscn")
const FEMALE_MODEL := preload("res://scenes/characters/female.tscn")

# Collision layers: 1 is world geometry, 2 is a character body, 4 is a weapon pick area.
const FLOOR_MASK := 1
const CHARACTER_MASK := 2
const WEAPON_MASK := 4

# Under Godot's default 0, so this tick publishes every actor's position and runs
# the one combat/regeneration clock before any actor's physics callback asks for
# a velocity.
const WORLD_PRIORITY := -10

# The spike picks the player's blessing by hand. Character creation will supply it
# later; no selection UI exists yet.
@export var player_blessing: int = 0

var _player: Player
var _camera: Camera3D
var _marker: MeshInstance3D
var _marker_tween: Tween
var _popup: CharacterPopup
var _attack_popup: AttackPopup
var _weapon_popup: WeaponPopup
var _technique_popup: TechniquePopup
var _equipment_popup: EquipmentPopup
var _workbench_popup: WorkbenchPopup
var _combat: OpenCombat
var _feedback: CombatFeedback

# The two benches the starter scene places, per docs/weapons.md: one clearly visible
# Smithing Workbench and one clearly visible Enchanting Workbench. Ids and types are the
# coordinator's observation schema; positions are recorded here only to hand to it.
const BENCH_DEFS := [
	{"id": 1, "type": 0, "name": "SmithingWorkbench",
			"position": Vector3(-9.0, 0.0, 5.0), "color": Color(0.45, 0.32, 0.20)},
	{"id": 2, "type": 1, "name": "EnchantingWorkbench",
			"position": Vector3(-9.0, 0.0, 9.0), "color": Color(0.28, 0.22, 0.45)},
]

# Presentation only: the bodies whose positions the coordinator is told about.
# Faction, health, ranks and orders stay in C++; nothing is mirrored here.
var _actors: Array[CharacterBody3D] = []


func _ready() -> void:
	process_physics_priority = WORLD_PRIORITY
	_build_world()
	_build_player()
	_build_standing_dummies()
	_build_combat()
	_build_workbenches()
	_build_camera()
	_build_marker()
	_build_hud()


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_I or event.physical_keycode == KEY_I:
			_toggle_popup()
			return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			if _popup_covers(mouse.position):
				return
			# A world click dismisses the action popup, then the same click goes on
			# to picking, so clicking beside it can still order a move.
			if _attack_popup != null:
				_attack_popup.close()
			if _weapon_popup != null:
				_weapon_popup.close()
			# A click outside the technique panel closes it the same way, and then goes on
			# to do what it would have done anyway: closing a panel is not an order.
			if _technique_popup != null:
				_technique_popup.close()
			if _equipment_popup != null:
				_equipment_popup.close()
			if _workbench_popup != null:
				_workbench_popup.close()
			_try_character_or_floor_click(mouse.position)


# The popups' own STOP mouse filter consumes clicks inside them in the running
# engine; this same check keeps picking correct even when the event reaches
# unhandled input, and makes the isolation testable headless.
func _popup_covers(point: Vector2) -> bool:
	if _popup != null and _popup.visible and _popup.get_global_rect().has_point(point):
		return true
	return (_attack_popup != null and _attack_popup.covers(point)) \
		or (_weapon_popup != null and _weapon_popup.covers(point)) \
		or (_technique_popup != null and _technique_popup.covers(point)) \
		or (_equipment_popup != null and _equipment_popup.covers(point)) \
		or (_workbench_popup != null and _workbench_popup.covers(point))


func _toggle_popup() -> void:
	if _popup == null:
		return
	_popup.visible = not _popup.visible


func _process(delta: float) -> void:
	if _player == null or _camera == null:
		return
	var target := _player.global_position + CAMERA_OFFSET
	_camera.global_position = _camera.global_position.lerp(target, 1.0 - exp(-CAMERA_FOLLOW * delta))
	_camera.look_at(_player.global_position + Vector3(0.0, CAMERA_LOOK_HEIGHT, 0.0), Vector3.UP)


# One physics frame, one clock. Positions go in first so C++ judges reach and
# pursuit against where the actors actually stand, then the session advances its
# own attack/defend clocks and regenerates each sheet once. Actors never tick
# either of those themselves.
func _physics_process(delta: float) -> void:
	if _combat == null:
		return
	for actor in _actors:
		if not is_instance_valid(actor):
			continue
		var sheet: OpenCharacter = actor.get("sheet")
		if sheet != null:
			_combat.set_position(sheet.get_character_id(), actor.global_position)
	_combat.step(delta)


# The technique panel is another view of the player's own sheet: it reads and writes that
# sheet's technique settings and nothing else. Orders, clocks and damage stay where they
# are, and no click on this panel can issue one.
func _on_techniques_requested(at: Vector2) -> void:
	if _technique_popup == null or _player == null or _player.sheet == null:
		return
	_technique_popup.open_for_sheet(_player.sheet, at)


func _on_equipment_requested(at: Vector2) -> void:
	if _equipment_popup == null or _player == null or _player.sheet == null:
		return
	_equipment_popup.open_for_sheet(_player.sheet, at)


func _on_equip_requested(item_id: int, slot: int) -> void:
	if _player == null or _player.sheet == null:
		return
	# A request, not an assumption: ownership, free hands, limbs and the two-hand reservation
	# are all decided there, and the panel reads back whatever was agreed to.
	_player.sheet.equip_item(item_id, slot)


func _on_unequip_requested(slot: int) -> void:
	if _player == null or _player.sheet == null:
		return
	_player.sheet.unequip_item(slot)


func _on_work_requested(request: Dictionary) -> void:
	if _combat == null or _player == null or _player.sheet == null:
		return
	# Carried to the coordinator unchanged: the panel named ids, the coordinator decides.
	_combat.request_work(_player.sheet.get_character_id(), request)


func _on_work_cancel_requested() -> void:
	if _combat == null or _player == null or _player.sheet == null:
		return
	_combat.cancel_work(_player.sheet.get_character_id())


func _on_attack_requested(target_id: int) -> void:
	if _combat == null or _player == null or _player.sheet == null:
		return
	# Godot sends the pending id and nothing else. C++ revalidates it against the
	# roster - dead, gone or self is refused, leaving the old order alone - and
	# decides reach, clocks and damage.
	if _combat.issue_attack(_player.sheet.get_character_id(), target_id):
		_marker.visible = false


func _try_character_or_floor_click(screen_pos: Vector2) -> void:
	# One ray, both masks: a character in front of the floor wins the pick, and a
	# body never falls through to the ground behind it.
	var from := _camera.project_ray_origin(screen_pos)
	var to := from + _camera.project_ray_normal(screen_pos) * 1000.0
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.collision_mask = FLOOR_MASK | CHARACTER_MASK | WEAPON_MASK
	query.collide_with_areas = true
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return
	var body: Node = hit.get("collider", null)
	if body is Area3D and body.has_meta("bench_id"):
		# A bench click is a bench click. In reach it opens that bench's panel; out of reach
		# it walks there through the coordinator and does not open anything - the approach is
		# the click, not a preview of it.
		var bench_id: int = int(body.get_meta("bench_id"))
		if _combat != null and _player != null and _player.sheet != null:
			var bench_position: Vector3 = (body.get_parent() as Node3D).global_position
			var gap := Vector2(_player.global_position.x - bench_position.x,
					_player.global_position.z - bench_position.z).length()
			if gap <= 1.5 + 0.001:
				_workbench_popup.open_for_bench(bench_id, int(body.get_meta("bench_type")),
						screen_pos)
			else:
				_combat.approach_workbench(_player.sheet.get_character_id(), bench_id)
		return
	if body is Area3D and body.collision_layer & WEAPON_MASK:
		_weapon_popup.open_for_model(body.get_parent().name, screen_pos)
		return
	if body is Player:
		return
	if body is StandingDummy:
		var dummy := body as StandingDummy
		# Living or dead is C++'s answer, and so is the faction gate there is not
		# one: any living character offers its action.
		if dummy.sheet != null and dummy.sheet.is_alive():
			_attack_popup.open_for_target(dummy.sheet.get_character_id(), screen_pos)
		return
	_try_move_to_click(screen_pos)


func _try_move_to_click(screen_pos: Vector2) -> void:
	var from := _camera.project_ray_origin(screen_pos)
	var to := from + _camera.project_ray_normal(screen_pos) * 1000.0
	var query := PhysicsRayQueryParameters3D.create(from, to)
	query.collision_mask = FLOOR_MASK
	query.collide_with_areas = false
	var hit := get_world_3d().direct_space_state.intersect_ray(query)
	if hit.is_empty():
		return
	var point: Vector3 = hit.position
	# The floor walk goes through the coordinator when there is one, so the same order
	# reaches the bench queue the way every other door does; the bare sheet call remains the
	# fixture fallback.
	if _combat != null and _player != null and _player.sheet != null:
		_combat.issue_move(_player.sheet.get_character_id(), point)
	else:
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
	floor_body.collision_layer = FLOOR_MASK
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
	body.collision_layer = FLOOR_MASK
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
	_actors.append(_player)


func _build_standing_dummies() -> void:
	var friend := StandingDummy.new()
	friend.name = "Friendly"
	friend.model_scene = FEMALE_MODEL
	friend.position = Vector3(-6.0, 0.1, -2.0)
	friend.setup(0, 1, 2, 2002)
	add_child(friend)
	_actors.append(friend)

	var enemy := StandingDummy.new()
	enemy.name = "Enemy"
	enemy.model_scene = FEMALE_MODEL
	enemy.position = Vector3(6.0, 0.1, -2.0)
	enemy.setup(1, 1, 3, 3003)
	add_child(enemy)
	_actors.append(enemy)


# The one combat session for this scene. Every sheet is registered once, with the
# position it starts at, and each actor gets the same reference so it can ask for
# its ordered velocity. The coordinator owns orders, clocks and regeneration.
func _build_combat() -> void:
	_combat = OpenCombat.new()
	for actor in _actors:
		var sheet: OpenCharacter = actor.get("sheet")
		if sheet == null:
			continue
		# Hand the coordinator over only if the sheet really joined it. An actor the
		# session never accepted would otherwise read zero velocity from
		# combat.velocity_at and freeze, instead of falling back the way a fixture
		# without a coordinator is documented to.
		if not _combat.add_character(sheet, actor.global_position):
			push_error("Character %d could not join the combat session" % sheet.get_character_id())
			continue
		actor.set("combat", _combat)

	# One setup command for the player's starting backpack. The table, the instance ids, the
	# tier and the durability are all C++'s, and this is the only thing in the scene that can
	# bring an item into existence: the panel below can move what is owned and nothing else.
	if _player != null and _player.sheet != null \
			and not _combat.setup_starter_player(_player.sheet.get_character_id()):
		push_error("The player's starter backpack could not be given")

	# The one thing in the scene that knows which sheet a human drives: this is the player, so
	# this is the sheet whose configured sequence chooses its attacks. A configured sequence on
	# a simulation-driven sheet is kept but not followed - which is what makes the handover the
	# scene's call and not a side effect of opening a panel.
	if _player != null and _player.sheet != null and not _player.sheet.set_player_controlled(true):
		push_error("The player's sheet could not be handed to the player")

	# What the fight looks like, reading the same coordinator the fight runs on. It computes
	# nothing and orders nothing: held meshes come from each sheet's equipment snapshot, and a
	# swing animates because C++ reported performing one. It ticks after this node (default
	# priority, and children follow their parent), so it reads the reports the step above made.
	_feedback = CombatFeedback.new()
	_feedback.name = "CombatFeedback"
	add_child(_feedback)
	_feedback.bind_combat(_combat, _actors)


func _build_workbenches() -> void:
	# Existing primitives only: a named table with a top, on a pick area, per the docs'
	# "one clearly visible" bench. The pick area lives on the scenery mask and carries the
	# coordinator's own observation ids, so a click reaches the same bench the step reads.
	var observations: Array = []
	for definition in BENCH_DEFS:
		var bench := Node3D.new()
		bench.name = String(definition["name"])
		bench.position = definition["position"]
		add_child(bench)

		var table := MeshInstance3D.new()
		var table_mesh := BoxMesh.new()
		table_mesh.size = Vector3(1.8, 0.9, 1.4)
		table.mesh = table_mesh
		table.position = Vector3(0.0, 0.45, 0.0)
		var table_material := StandardMaterial3D.new()
		table_material.albedo_color = definition["color"]
		table_material.roughness = 0.8
		table.material_override = table_material
		bench.add_child(table)

		var legs := MeshInstance3D.new()
		var leg_mesh := BoxMesh.new()
		leg_mesh.size = Vector3(1.9, 0.18, 1.5)
		legs.mesh = leg_mesh
		legs.position = Vector3(0.0, 0.09, 0.0)
		legs.material_override = table_material
		bench.add_child(legs)

		var area := Area3D.new()
		area.name = "BenchPickArea"
		area.collision_layer = WEAPON_MASK
		area.collision_mask = 0
		area.set_meta("bench_id", int(definition["id"]))
		area.set_meta("bench_type", int(definition["type"]))
		bench.add_child(area)
		var collision := CollisionShape3D.new()
		var shape := BoxShape3D.new()
		shape.size = Vector3(2.2, 1.4, 1.8)
		collision.shape = shape
		collision.position = Vector3(0.0, 0.7, 0.0)
		area.add_child(collision)

		observations.append({"id": int(definition["id"]), "type": int(definition["type"]),
				"position": definition["position"]})
	if _combat != null and not _combat.update_workbenches(observations):
		push_error("The bench observations were refused by the coordinator")


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
	label.text = "Left click the floor to move. Left click a character to attack. I opens the character sheet."
	label.position = Vector2(24, 20)
	label.add_theme_font_size_override("font_size", 22)
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(label)

	_popup = CharacterPopup.new()
	_popup.name = "CharacterPopup"
	_popup.visible = false
	layer.add_child(_popup)
	_popup.bind_sheet(_player.sheet)

	# The equipment panel is a view of the player's own sheet: it lists what the sheet owns and
	# asks C++ to move things between the hands. Whether any of that happens is C++'s answer.
	_equipment_popup = EquipmentPopup.new()
	_equipment_popup.name = "EquipmentPopup"
	_equipment_popup.visible = false
	layer.add_child(_equipment_popup)
	_equipment_popup.bind_sheet(_player.sheet)
	_popup.equipment_requested.connect(_on_equipment_requested)
	_equipment_popup.equip_requested.connect(_on_equip_requested)
	_equipment_popup.unequip_requested.connect(_on_unequip_requested)

	# The bench panel is the same shape as the other two: a view of the player's own working
	# picture, sending requests the coordinator answers or refuses.
	_workbench_popup = WorkbenchPopup.new()
	_workbench_popup.name = "WorkbenchPopup"
	_workbench_popup.visible = false
	layer.add_child(_workbench_popup)
	_workbench_popup.bind_worker(_combat, _player.sheet)
	_workbench_popup.work_requested.connect(_on_work_requested)
	_workbench_popup.cancel_requested.connect(_on_work_cancel_requested)

	_attack_popup = AttackPopup.new()
	_attack_popup.name = "AttackPopup"
	_attack_popup.visible = false
	layer.add_child(_attack_popup)
	_attack_popup.attack_requested.connect(_on_attack_requested)

	_technique_popup = TechniquePopup.new()
	_technique_popup.name = "TechniquePopup"
	_technique_popup.visible = false
	layer.add_child(_technique_popup)
	_technique_popup.bind_sheet(_player.sheet)
	_popup.techniques_requested.connect(_on_techniques_requested)

	_weapon_popup = WeaponPopup.new()
	_weapon_popup.name = "WeaponPopup"
	_weapon_popup.visible = false
	layer.add_child(_weapon_popup)


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
