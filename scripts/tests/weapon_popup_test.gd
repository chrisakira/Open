extends SceneTree

var failures := 0
var requests: Array[String] = []


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _click(point: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = point
	motion.global_position = point
	root.push_input(motion, true)
	for pressed in [true, false]:
		var event := InputEventMouseButton.new()
		event.button_index = MOUSE_BUTTON_LEFT
		event.position = point
		event.global_position = point
		event.pressed = pressed
		root.push_input(event, true)


func _requested(model_name: String) -> void:
	requests.append(model_name)


func _run() -> void:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	var popups := world.find_children("WeaponPopup", "", true, false)
	_check(popups.size() == 1, "the world has a weapon action popup")
	if popups.size() != 1:
		world.queue_free()
		await process_frame
		quit(1)
		return
	var popup: Control = popups[0]
	_check(popup.has_signal("equip_requested"), "the popup exposes an equip trigger")
	if popup.has_signal("equip_requested"):
		popup.connect("equip_requested", _requested)
	var camera: Camera3D = world.get("_camera")
	var player: Node = world.get_node("Player")
	var marker: Node = world.get("_marker")
	world.set_process(false)
	for model in world.get_node("MedievalWeapons").get_children():
		var model_name: String = model.name
		camera.global_position = model.global_position + Vector3(0.0, 7.0, 6.0)
		camera.look_at(model.global_position, Vector3.UP)
		await physics_frame
		var point := camera.unproject_position(model.global_position + Vector3(0.0, 0.08, 0.0))
		_click(point)
		_check(popup.visible, "clicking %s opens the equip popup" % model_name)
		_check(not marker.visible, "clicking a weapon does not order a move")
		_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO, "weapon click leaves movement unchanged")
		if popup.visible:
			await process_frame
			var buttons := popup.find_children("*", "Button", true, false)
			_check(buttons.size() == 1 and "equip" in buttons[0].text.to_lower(), "popup offers Equip")
			if buttons.size() == 1:
				_click(buttons[0].get_global_rect().get_center())
				_check(not popup.visible, "Equip closes the popup")
				_check(requests.size() > 0 and requests.back() == model_name, "Equip triggers for the clicked model")
				_check(not marker.visible and player.sheet.velocity_at(player.global_position) == Vector3.ZERO, "Equip issues no movement")
				_check(player.sheet.get_attack_target_id() == 0, "Equip issues no attack")
				_check(is_instance_valid(model) and model.is_inside_tree(), "Equip leaves the display model in place")
	world.queue_free()
	await process_frame
	if failures == 0:
		print("Weapon popup: all checks passed")
	quit(1 if failures else 0)
