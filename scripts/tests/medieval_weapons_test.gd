extends SceneTree


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	var display := world.get_node_or_null("MedievalWeapons")
	if display == null:
		printerr("FAIL: the main scene has no medieval weapons display")
		world.queue_free()
		quit(1)
		return
	var models := display.get_children()
	if models.size() != 24:
		printerr("FAIL: expected all 24 models, got ", models.size())
		world.queue_free()
		quit(1)
		return
	for model in models:
		if model.find_children("*", "MeshInstance3D", true, false).is_empty():
			printerr("FAIL: no visible mesh for ", model.name)
			world.queue_free()
			quit(1)
			return
	world.queue_free()
	await process_frame
	print("Medieval weapons display: all 24 models present")
	quit(0)
