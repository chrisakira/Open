extends Node3D

# The display is scenery only. It does not create or equip simulation items.
const MODELS := [
	"Arrow", "Axe", "Axe_Double", "Axe_Small", "Bow_Evil", "Bow_Golden",
	"Bow_Wooden", "Bow_Wooden2", "Claymore", "Dagger", "Dagger_2", "Hammer_Double",
	"Hammer_Small", "Scythe", "Shield_Celtic_Golden", "Shield_Heater", "Shield_Heater_2",
	"Shield_Round", "Shield_Round_2", "Spear", "Sword", "Sword_2", "Sword_Big", "Sword_Golden",
]
const MODEL_SCALE := 0.22


func _ready() -> void:
	for i in MODELS.size():
		var model_name: String = MODELS[i]
		var mesh := load("res://assets/weapons/quaternius_medieval/%s.obj" % model_name) as Mesh
		if mesh == null:
			push_error("Could not load medieval weapon: " + model_name)
			continue
		var model := Node3D.new()
		model.name = model_name
		model.position = Vector3((i % 6) * 1.8, 0.04, (i / 6) * 2.5)
		add_child(model)
		var visual := MeshInstance3D.new()
		visual.mesh = mesh
		visual.scale = Vector3.ONE * MODEL_SCALE
		# The arrow already lies along Z; the other models' long axis is Y.
		if model_name != "Arrow":
			visual.rotation_degrees.x = 90.0
		model.add_child(visual)
		# Center each model in its own cell and keep the lowest point above the floor.
		var bounds := visual.transform * mesh.get_aabb()
		visual.position = Vector3(-bounds.get_center().x, -bounds.position.y, -bounds.get_center().z)

		# A pick-only area does not block characters or intercept floor movement rays.
		var area := Area3D.new()
		area.name = "WeaponPickArea"
		area.collision_layer = 4
		area.collision_mask = 0
		model.add_child(area)
		var collision := CollisionShape3D.new()
		var box := BoxShape3D.new()
		box.size = Vector3(maxf(bounds.size.x, 0.35), maxf(bounds.size.y, 0.28), maxf(bounds.size.z, 0.35))
		collision.shape = box
		collision.position.y = box.size.y / 2.0
		area.add_child(collision)
