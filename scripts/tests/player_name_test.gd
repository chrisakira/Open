extends SceneTree


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	var player = world.get("_player")
	if player == null or player.sheet == null:
		printerr("FAIL: player and sheet must exist")
		world.queue_free()
		await process_frame
		quit(1)
		return
	var actual: String = player.sheet.get_character_name()
	world.queue_free()
	await process_frame
	if actual != "John Doe":
		printerr("FAIL: player sheet name should be John Doe, got '", actual, "'")
		quit(1)
		return
	print("PASS: player sheet name is John Doe")
	quit(0)
