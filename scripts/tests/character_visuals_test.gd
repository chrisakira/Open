extends SceneTree

# Task 8 scene tests. The visual contract of the spike must survive the sheet
# migration: same models, same collision, same relaxed pose and walk animation,
# same stationary NPCs, and now every character driven by a C++ sheet.

var failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _mesh_count(node: Node) -> int:
	var count := 1 if node is MeshInstance3D else 0
	for child in node.get_children():
		count += _mesh_count(child)
	return count


func _find_label(node: Node) -> Label3D:
	for child in node.get_children():
		if child is Label3D:
			return child as Label3D
	return null


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

	# Every character is driven by a configured C++ sheet.
	for character in [player, ally, enemy]:
		_check(character.sheet != null and character.sheet.is_configured(), character.name + " has a configured sheet")
		_check(character.sheet.get_blessing() >= 0 and character.sheet.get_blessing() <= 9,
			character.name + " carries one of the ten blessings")
		_check(character.sheet.get_health() > 0.0 and character.sheet.is_alive(),
			character.name + " starts alive")
	_check(player.sheet.get_character_id() == 1, "the player uses the setup id")
	_check(ally.sheet.get_character_id() == 2, "the friendly npc uses its setup id")
	_check(enemy.sheet.get_character_id() == 3, "the enemy npc uses its setup id")
	_check(ally.sheet.get_character_id() != enemy.sheet.get_character_id(), "setup ids are distinct")
	_check(world.get("player_blessing") == 0, "the spike default blessing is Strength")
	_check(player.sheet.get_blessing() == int(world.get("player_blessing")), "the player uses the explicit selection")
	_check(player.sheet.get_level() == 1, "the spike player starts at level one")
	_check(ally.sheet.get_level() == 1 and enemy.sheet.get_level() == 1, "the spike npcs start at level one")

	# The NPC blessings come from their seeds, so a rerun names the same blessing.
	var replay_ally := StandingDummy.new()
	replay_ally.model_scene = ally.get("model_scene")
	replay_ally.setup(0, 1, 2, 2002)
	var replay_enemy := StandingDummy.new()
	replay_enemy.model_scene = ally.get("model_scene")
	replay_enemy.setup(1, 1, 3, 3003)
	root.add_child(replay_ally)
	root.add_child(replay_enemy)
	_check(replay_ally.sheet.is_configured() and replay_enemy.sheet.is_configured(),
		"seeded npc sheets configure")
	_check(replay_ally.sheet.get_blessing() == ally.sheet.get_blessing(), "one seed gives one blessing")
	_check(replay_enemy.sheet.get_blessing() == enemy.sheet.get_blessing(), "and the same for the enemy")
	replay_ally.queue_free()
	replay_enemy.queue_free()

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
	_check(_close(player.sheet.get_walk_speed(), 4.5), "the walk speed now comes from the sheet")

	# Coverage gap found in Task 9: the spike's only input path. A floor click must
	# still reach C++ as a move order, and the marker must still show.
	var camera: Camera3D = world.get("_camera")
	_check(camera != null, "the world still owns its camera")
	var marker: Node = world.get("_marker")
	_check(marker != null and not marker.visible, "the marker starts hidden")
	if camera != null:
		var clicked: Vector3 = player.global_position + Vector3(3.0, 0.0, 0.0)
		var screen: Vector2 = camera.unproject_position(clicked)
		world._try_move_to_click(screen)
		_check(marker != null and marker.visible, "a floor click shows the marker")
		await physics_frame
		var ordered: Vector3 = player.sheet.velocity_at(player.global_position)
		_check(ordered.length() > 0.0, "the click became a C++ move order")
		for frame in range(90):
			await physics_frame
		_check(absf(player.global_position.x - clicked.x) < 1.0, "the sheet walks to the clicked point")
		_check(absf(player.global_position.z - clicked.z) < 1.0, "without drifting sideways")

	var other := Player.new()
	var alternate := PackedScene.new()
	var alternate_node := MeshInstance3D.new()
	alternate_node.mesh = BoxMesh.new()
	alternate.pack(alternate_node)
	alternate_node.free()
	other.set("model_scene", alternate)
	other.set("character_id", 11)
	root.add_child(other)
	await process_frame
	var other_visual := other.get_node_or_null("Visual")
	_check(other_visual != null and _mesh_count(other_visual) == 1, "a character can use an independently assigned model")
	_check(player.get("model_scene") != alternate, "changing one character does not change another")
	_check(other.sheet.get_character_id() == 11, "a character takes its own setup id")
	var other_dummy := StandingDummy.new()
	other_dummy.model_scene = alternate
	other_dummy.setup(0, 1, 12, 4004)
	root.add_child(other_dummy)
	_check(_mesh_count(other_dummy.get_node("Visual")) == 1, "a standing character can use an independently assigned model")
	_check(ally.get("model_scene") != alternate, "changing one standing character does not change another")
	_check(other_dummy.sheet.get_character_id() == 12, "a standing character takes its own setup id")
	_check(not other_dummy.sheet.get_can_move(), "and stays stationary")
	other_dummy.queue_free()
	other.queue_free()

	# Fresh fixtures for injury: the scene must obey C++ speed and death.
	var walker := Player.new()
	root.add_child(walker)
	await process_frame
	walker.go_to(walker.global_position + Vector3(8.0, 0.0, 0.0))
	await physics_frame
	_check(_close(walker.sheet.velocity_at(walker.global_position).x, 4.5), "a fresh walker moves at the sheet speed")
	_check(walker.sheet.apply_damage(4, 40.0), "one leg hit lands")
	await physics_frame
	_check(_close(walker.sheet.get_walk_speed(), 2.25), "one leg hit halves the speed")
	var half_speed: float = walker.sheet.velocity_at(walker.global_position).x
	_check(half_speed > 0.0 and half_speed < 3.0, "the walker really moves slower")
	await physics_frame
	_check(walker.sheet.apply_damage(5, 40.0), "the other leg hit lands")
	_check(_close(walker.sheet.get_health(), 20.0), "two leg hits leave twenty hp")
	_check(walker.sheet.is_alive(), "legs alone do not kill")
	_check(not walker.sheet.can_walk(), "both legs gone stops walking")
	await physics_frame
	var stopped: Vector3 = walker.sheet.velocity_at(walker.global_position)
	_check(stopped == Vector3.ZERO, "and the sheet order stops")

	var killed := Player.new()
	root.add_child(killed)
	await process_frame
	killed.go_to(killed.global_position + Vector3(8.0, 0.0, 0.0))
	await physics_frame
	_check(killed.sheet.apply_damage(0, 25.0), "a head hit lands")
	_check(_close(killed.sheet.get_health(), 75.0), "main hp paid once, leaving seventy-five")
	_check(not killed.sheet.is_alive(), "a zero head kills")
	_check(not killed.sheet.can_walk(), "a dead character cannot walk")
	var killed_before: Vector3 = killed.global_position
	for frame in range(20):
		await physics_frame
	_check(killed.global_position.distance_to(killed_before) < 0.001, "a dead character does not move")

	# The standing label is presentation, and it must follow the C++ numbers.
	var ally_label := _find_label(ally)
	_check(ally_label != null, "the standing character still has its label")
	if ally_label != null:
		_check(ally_label.text.contains("HP 100"), "the label starts at full health")
		var before := ally_label.text
		_check(ally.sheet.apply_damage(1, 10.0), "a torso hit lands on the standing character")
		await physics_frame
		_check(ally_label.text != before, "the label refreshes when health changes")
		_check(ally_label.text.contains("HP 90"), "the label shows the new health")
		_check(ally_label.text.contains("Friendly") or ally_label.text.contains("Lv"),
			"the label keeps its identity text")
		_check(_close(ally.sheet.get_health(), 90.0), "and C++ agrees")

	world.queue_free()
	await process_frame
	if failures == 0:
		print("Character visuals: all checks passed")
	quit(1 if failures else 0)
