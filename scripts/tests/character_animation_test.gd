extends SceneTree

var failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _run() -> void:
	for path in ["res://scenes/characters/male.tscn", "res://scenes/characters/female.tscn"]:
		var model: Node = load(path).instantiate()
		root.add_child(model)
		await process_frame
		var players := model.find_children("*", "AnimationPlayer", true, false)
		_check(not players.is_empty(), path + " provides an animation player")
		if not players.is_empty():
			var player: AnimationPlayer = players[0]
			var idle := player.current_animation
			_check(player.is_playing() and not idle.is_empty(), path + " plays an idle")
			model.update_walk(0.1, 1.0)
			_check(player.is_playing() and player.current_animation != idle, path + " switches to walking")
			_check(player.current_animation == &"Walk", path + " uses the normal walk rather than carrying")
			var skeleton: Skeleton3D = model.find_children("*", "Skeleton3D", true, false)[0]
			var leg := skeleton.find_bone("thigh_l")
			player.advance(0.4)
			var first := skeleton.get_bone_pose_rotation(leg)
			player.advance(0.4)
			_check(absf(first.dot(skeleton.get_bone_pose_rotation(leg))) < 0.9999, path + " animates the model's leg")
			model.update_walk(0.1, 0.0)
			_check(player.current_animation == idle, path + " returns to idle")
			var attachments := model.find_children("*", "BoneAttachment3D", true, false)
			_check(not attachments.is_empty() and attachments[0].get_bone_name() == "Head", path + " attaches hair to the head bone")
			if not attachments.is_empty():
				_check(attachments[0].get_node_or_null("Hair") != null, path + " keeps the hair on the attachment")
		model.queue_free()

	# The scene's exported choices can be changed before the model is added.
	var alternate: Node = load("res://scenes/characters/male.tscn").instantiate()
	alternate.set("idle_animation", &"Zombie_Idle")
	alternate.set("walk_animation", &"Zombie_Walk_Fwd")
	root.add_child(alternate)
	await process_frame
	var alternate_players := alternate.find_children("*", "AnimationPlayer", true, false)
	if not alternate_players.is_empty():
		var player: AnimationPlayer = alternate_players[0]
		_check(player.current_animation == &"Zombie_Idle", "alternate idle is selectable")
		alternate.update_walk(0.1, 1.0)
		_check(player.current_animation == &"Zombie_Walk_Fwd", "alternate walk is selectable")
	else:
		_check(false, "alternate model has an animation player")
	alternate.queue_free()

	var mixed: Node = load("res://scenes/characters/male.tscn").instantiate()
	mixed.set("idle_animation", &"Idle_FoldArms")
	mixed.set("walk_animation", &"Jog_Fwd")
	root.add_child(mixed)
	await process_frame
	var mixed_players := mixed.find_children("*", "AnimationPlayer", true, false)
	if not mixed_players.is_empty():
		var player: AnimationPlayer = mixed_players[0]
		_check(player.current_animation == &"Idle_FoldArms", "library 2 idle remains selectable")
		mixed.update_walk(0.1, 1.0)
		_check(player.current_animation == &"Jog_Fwd", "a different library 1 movement is selectable")
	else:
		_check(false, "mixed-library model has an animation player")
	mixed.queue_free()
	await process_frame
	if failures == 0:
		print("Character animation: all checks passed")
	quit(1 if failures else 0)
