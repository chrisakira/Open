extends SceneTree

var failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _mesh_count(node: Node) -> int:
	var count := 1 if node is MeshInstance3D else 0
	for child in node.get_children():
		count += _mesh_count(child)
	return count


func _run() -> void:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	var player: Node = world.get_node("Player")
	var ally: Node = world.get_node("Friendly")
	var enemy: Node = world.get_node("Enemy")
	for character in [player, ally, enemy]:
		var visual: Node = character.get_node_or_null("Visual")
		_check(character.get("model_scene") is PackedScene, character.name + " has an assigned model scene")
		_check(visual != null and _mesh_count(visual) > 0, character.name + " has visible geometry")
		_check(character.find_children("*", "CollisionShape3D", true, false).size() > 0, character.name + " retains collision")
		if visual:
			var skeletons := visual.find_children("*", "Skeleton3D", true, false)
			if not skeletons.is_empty():
				var skeleton: Skeleton3D = skeletons[0]
				for side in ["l", "r"]:
					var upper := skeleton.find_bone("upperarm_" + side)
					var lower := skeleton.find_bone("lowerarm_" + side)
					if upper >= 0 and lower >= 0:
						_check(skeleton.get_bone_global_pose(lower).origin.y < skeleton.get_bone_global_pose(upper).origin.y - 0.1, character.name + " rests its " + side + " arm down")
	_check(ally.get("model_scene") == enemy.get("model_scene") and ally.get("model_scene") != null, "both standing characters use the female model")
	_check(player.get("model_scene") != null and player.get("model_scene") != ally.get("model_scene"), "player uses a different model")
	_check(ally.sheet.get_faction() == 0 and enemy.sheet.get_faction() == 1, "factions remain unchanged")
	_check(not ally.sheet.get_can_move() and not enemy.sheet.get_can_move(), "standing characters cannot move")
	var ally_start: Vector3 = ally.global_position
	var enemy_start: Vector3 = enemy.global_position
	var start: Vector3 = player.global_position
	var player_skeleton: Skeleton3D = player.get_node("Visual").find_children("*", "Skeleton3D", true, false)[0]
	var left_leg := player_skeleton.find_bone("thigh_l")
	var idle_rotation := player_skeleton.get_bone_pose_rotation(left_leg)
	player.go_to(start + Vector3(3.0, 0.0, 0.0))
	for frame in range(8):
		await physics_frame
	var first_step := player_skeleton.get_bone_pose_rotation(left_leg)
	var greatest_change := 1.0
	for frame in range(16):
		await physics_frame
		greatest_change = minf(greatest_change, absf(first_step.dot(player_skeleton.get_bone_pose_rotation(left_leg))))
	_check(greatest_change < 0.999, "player leg pose changes while walking")
	for frame in range(160):
		await physics_frame
	_check(player.global_position.x > start.x + 1.0, "player follows a move order")
	_check(absf(idle_rotation.dot(player_skeleton.get_bone_pose_rotation(left_leg))) > 0.9999, "player returns to the idle pose after arriving")
	_check(absf(ally.global_position.x - ally_start.x) < 0.01 and absf(enemy.global_position.x - enemy_start.x) < 0.01, "standing characters remain in place")
	var other := Player.new()
	var alternate := PackedScene.new()
	var alternate_node := MeshInstance3D.new()
	alternate_node.mesh = BoxMesh.new()
	alternate.pack(alternate_node)
	alternate_node.free()
	other.set("model_scene", alternate)
	root.add_child(other)
	await process_frame
	var other_visual := other.get_node_or_null("Visual")
	_check(other_visual != null and _mesh_count(other_visual) == 1, "a character can use an independently assigned model")
	_check(player.get("model_scene") != alternate, "changing one character does not change another")
	var other_dummy := StandingDummy.new()
	other_dummy.model_scene = alternate
	other_dummy.setup(0, 1)
	root.add_child(other_dummy)
	_check(_mesh_count(other_dummy.get_node("Visual")) == 1, "a standing character can use an independently assigned model")
	_check(ally.get("model_scene") != alternate, "changing one standing character does not change another")
	other_dummy.queue_free()
	other.queue_free()
	world.queue_free()
	await process_frame
	if failures == 0:
		print("Character visuals: all checks passed")
	quit(1 if failures else 0)
