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
			# The combat set: the clips exist, armed idles choose, walk yields to a swing,
			# and playback wears the duration C++ named for that very swing.
			for clip in ["Sword_Idle", "Idle_Shield", "Sword_Regular_A", "Sword_Regular_B",
					"Sword_Heavy_Combo", "Punch_Jab"]:
				_check(player.has_animation(clip), path + " carries " + clip)
			_check(player.get_animation(&"Sword_Idle").loop_mode == Animation.LOOP_LINEAR,
					path + " loops the sword idle without touching the asset")
			_check(player.get_animation(&"Sword_Regular_A").loop_mode == Animation.LOOP_NONE,
					path + " leaves its attack clips one-shot")

			model.set_armed_idle(&"weapon")
			_check(player.current_animation == &"Sword_Idle", path + " stands the armed way")
			model.set_armed_idle(&"shield")
			_check(player.current_animation == &"Idle_Shield", path + " stands the shielded way")
			model.set_armed_idle(&"unarmed")
			_check(player.current_animation == idle, path + " stands down to its exported idle")

			model.play_attack(1, 2.0, false)
			_check(player.current_animation == &"Sword_Regular_A",
					path + " swings regular A for a slash")
			var clip_length: float = player.get_animation(&"Sword_Regular_A").length
			_check(absf(player.speed_scale - clip_length / 2.0) < 0.0001,
					path + " scales the clip to the two seconds C++ gave it")
			model.update_walk(0.1, 1.0)
			_check(player.current_animation == &"Sword_Regular_A",
					path + " lets the walk pass through a swing")
			_check(absf(player.speed_scale - clip_length / 2.0) < 0.0001,
					path + " keeps the swing's own speed through that walk call")

			# A repeat of the same clip starts at zero, not where it was left.
			player.advance(0.4)
			model.play_attack(1, 2.0, false)
			_check(absf(player.get_current_animation_position()) < 0.001,
					path + " restarts a repeated clip at time zero")

			# Finishing hands the body back to the latest walk and its speed.
			model.update_walk(0.1, 0.6)
			for step in range(200):
				if player.current_animation != &"Sword_Regular_A":
					break
				player.advance(0.1)
			await process_frame
			_check(player.current_animation == &"Walk", path + " resumes walking after the swing")
			_check(absf(player.speed_scale - 0.6) < 0.0001,
					path + " restores the latest locomotion speed")

			# The map, and the duration guards.
			model.play_attack(0, 1.0, false)
			_check(player.current_animation == &"Sword_Regular_A", path + " basic is regular A")
			model.play_attack(2, 1.0, false)
			_check(player.current_animation == &"Sword_Regular_B", path + " thrust is regular B")
			model.play_attack(3, 1.0, false)
			_check(player.current_animation == &"Sword_Heavy_Combo",
					path + " heavy strike is the combo")
			model.play_attack(0, 1.0, true)
			_check(player.current_animation == &"Punch_Jab", path + " bare hands jab")
			model.play_attack(42, 1.0, false)
			_check(player.current_animation == &"Sword_Regular_A",
					path + " an unknown id falls back to basic")
			model.play_attack(1, 0.0, false)
			_check(absf(player.speed_scale - 1.0) < 0.0001,
					path + " a zero duration plays at speed one")
			model.play_attack(1, INF, false)
			_check(absf(player.speed_scale - 1.0) < 0.0001,
					path + " an infinite duration plays at speed one")
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
