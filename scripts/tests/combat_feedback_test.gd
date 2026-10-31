extends SceneTree

# Task 15: visible combat. What each sheet holds, the swings it performed, and the damage
# that actually arrived.
#
# CombatFeedback reads reports and draws them; it decides nothing. So every number below is
# checked against the simulation's own book, never against a second copy: a held mesh has to
# match what the sheet's equipment snapshot says, a motion has to match a performed swing and
# only that one, and the sum of the numbers on screen has to equal the HP that C++ lost. A
# label that also applied damage, or a motion for a swing the simulation refused, fails here.
#
# The feedback node and its two methods are reached by node lookup and duck-typed calls, so a
# missing node or missing method is an assertion in this suite rather than a parse cascade.

var failures := 0
# A runtime error aborts the rest of a case, and a suite that only counts failures could then
# be quietly green about nothing. Every case is asked to reach a floor.
var checks := 0

const TYPED_BLOW := 0.04 / 1.05
const SLOT_LEFT_HAND := 5
const SLOT_RIGHT_HAND := 6
const PART_RIGHT_ARM := 3

# Starter model key -> hand rule, from the production table: only the spear takes both hands.
const STARTER_HELD := {
	"Sword": false, "Dagger": false, "Spear": true, "Hammer_Small": false, "Shield_Round": false,
}


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- the world and its feedback node ------------------------------------------

func _world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	_check(world.get_node("Player") != null, "the world built its player")
	_check(world.get("_combat") != null, "and its coordinator")
	return world


func _free(world: Node) -> void:
	world.queue_free()
	await process_frame
	await process_frame


func _feedback_of(world: Node):
	var found := world.find_children("CombatFeedback", "", true, false)
	_check(found.size() == 1, "the world carries exactly one combat feedback node")
	if found.size() != 1:
		return null
	var feedback = found[0]
	_check(feedback is Node3D, "and it lives in the 3D world, not on the hud")
	for method in ["bind_combat", "swing_motion_count"]:
		_check(feedback.has_method(method), "the feedback node offers %s()" % method)
	return feedback


func _tick(node, delta: float) -> void:
	node.call("_physics_process", delta)


func _sheet_of(world: Node, actor_name: String):
	return world.get_node(actor_name).get("sheet")


func _id_of_model(sheet, model_key: String) -> int:
	for row in sheet.get_equipment_snapshot():
		if String(row["model_key"]) == model_key:
			return int(row["item_id"])
	return 0


func _row_of(sheet, item_id: int) -> Dictionary:
	for row in sheet.get_equipment_snapshot():
		if int(row["item_id"]) == item_id:
			return row
	return {}


func _held_meshes(world: Node) -> Array:
	return world.find_children("Held_*", "MeshInstance3D", true, false)


func _mesh_count_of(meshes: Array, model_key: String) -> int:
	var count := 0
	for mesh in meshes:
		if String(mesh.get_meta("model_key", "")) == model_key:
			count += 1
	return count


func _mesh_for(meshes: Array, model_key: String):
	for mesh in meshes:
		if String(mesh.get_meta("model_key", "")) == model_key:
			return mesh
	return null


func _socket_of(mesh) -> String:
	# Which hand the mesh hangs from, by its parent: the fact under test is the hand, not the
	# exact local pose.
	var parent: Node = mesh.get_parent()
	while parent != null and not (parent is Window):
		if String(parent.name) == "LeftHandSocket" or String(parent.name) == "RightHandSocket":
			return String(parent.name)
		parent = parent.get_parent()
	return ""


func _labels_of(feedback) -> Array:
	return feedback.find_children("DamageLabel*", "Label3D", true, false)


func _socket_bone(mesh) -> String:
	# The bone the mesh's socket rides, by walking up to the named socket and reading
	# what kind of node it is. A socket that is not a BoneAttachment3D answers "".
	var parent: Node = mesh.get_parent()
	while parent != null and not (parent is Window):
		if String(parent.name) in ["LeftHandSocket", "RightHandSocket"]:
			if parent is BoneAttachment3D:
				return String(parent.bone_name)
			return ""
		parent = parent.get_parent()
	return ""


func _meshes_hang_from_the_hand_bones_themselves() -> void:
	var world: Node = await _world()
	var feedback = _feedback_of(world)
	var sheet = _sheet_of(world, "Player")
	var player: Node = world.get_node("Player")
	if feedback == null:
		await _free(world)
		return

	# Right hand: the mesh's socket is a bone attachment on hand_r, and attaching
	# touches nothing else about the body.
	_check(sheet.equip_item(_id_of_model(sheet, "Sword"), SLOT_RIGHT_HAND), "the sword is held")
	_tick(feedback, 0.1)
	var sword = _mesh_for(_held_meshes(world), "Sword")
	_check(sword != null, "the sword is in hand")
	if sword != null:
		_check(_socket_bone(sword) == "hand_r",
				"and it hangs from the hand_r bone, not from a Visual offset")
	_check(is_zero_approx(player.get_node("Visual").rotation_degrees.x),
			"attaching left the body itself straight")

	# Left hand: the same bone rule names the other side.
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "the sword comes off")
	_check(sheet.equip_item(_id_of_model(sheet, "Sword"), SLOT_LEFT_HAND), "the sword goes left")
	_tick(feedback, 0.1)
	var moved = _mesh_for(_held_meshes(world), "Sword")
	if moved != null:
		_check(_socket_bone(moved) == "hand_l", "the mesh follows onto hand_l")
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "sword off again")

	# The spear names both hands and stays one mesh on the right.
	_check(sheet.equip_item(_id_of_model(sheet, "Spear"), SLOT_LEFT_HAND), "the spear goes left")
	_tick(feedback, 0.1)
	var spear_meshes: Array = []
	for mesh in _held_meshes(world):
		if String(mesh.get_meta("model_key", "")) == "Spear":
			spear_meshes.append(mesh)
	_check(spear_meshes.size() == 1, "a two-hander is still one mesh")
	if spear_meshes.size() == 1:
		_check(_socket_bone(spear_meshes[0]) == "hand_r",
				"and the one mesh rides hand_r, whatever slot opened the door")
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "the spear comes back off")

	# A hand change inside one tick: off and on across the body with no feedback
	# pass in between, and the mesh still ends up exactly one, on the new bone.
	_check(sheet.equip_item(_id_of_model(sheet, "Dagger"), SLOT_RIGHT_HAND), "the dagger goes right")
	_tick(feedback, 0.1)
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "the dagger leaves the right")
	_check(sheet.equip_item(_id_of_model(sheet, "Dagger"), SLOT_LEFT_HAND),
			"and lands left before anything draws")
	_tick(feedback, 0.1)
	var daggers: Array = []
	for mesh in _held_meshes(world):
		if String(mesh.get_meta("model_key", "")) == "Dagger":
			daggers.append(mesh)
	_check(daggers.size() == 1, "one dagger mesh, no duplicate behind")
	if daggers.size() == 1:
		_check(_socket_bone(daggers[0]) == "hand_l", "and it moved with the hand")
	# The grip table itself: every starter model has an explicit, finite, positive-scale
	# entry, because a silent fallback for Sword would be a float that no one chose.
	for key in ["Sword", "Dagger", "Spear", "Hammer_Small", "Shield_Round"]:
		var grip: Dictionary = CombatFeedback.GRIPS.get(key, {})
		_check(not grip.is_empty(), "%s has an explicit grip entry" % key)
		if grip.is_empty():
			continue
		var offset: Vector3 = grip["offset"]
		var spin: Vector3 = grip["rotation_degrees"]
		_check(is_finite(offset.x) and is_finite(offset.y) and is_finite(offset.z),
				"%s grip offset is finite" % key)
		_check(is_finite(spin.x) and is_finite(spin.y) and is_finite(spin.z),
				"%s grip rotation is finite" % key)
		_check(float(grip["scale"]) > 0.0 and is_finite(float(grip["scale"])),
				"%s grip scale is positive and finite" % key)
	await _free(world)


# --- held models ---------------------------------------------------------------

func _the_world_owns_a_feedback_node() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	# Nobody wears anything at the start - setup mints a backpack, not a mannequin - so there is
	# nothing to hang yet, and no socket may claim a hand the sheet says is empty.
	_check(_held_meshes(world).is_empty(), "an owned-but-worn-by-nothing player shows no held mesh")
	# Opening and closing a panel is not an equip: a fresh feedback bind draws the same nothing.
	var combat = world.get("_combat")
	var actors: Array = world.get("_actors")
	feedback.call("bind_combat", combat, actors)
	await physics_frame
	await physics_frame
	_check(_held_meshes(world).is_empty(), "and rebinding invents no mesh either")
	await _free(world)


func _every_starter_item_shows_in_the_hand_that_holds_it() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	var sheet = _sheet_of(world, "Player")
	for model in STARTER_HELD.keys():
		var item := _id_of_model(sheet, String(model))
		_check(item != 0, "the player owns the %s" % String(model))
	_check(sheet.equip_item(_id_of_model(sheet, "Sword"), SLOT_RIGHT_HAND),
			"the sword goes into the right hand")
	await physics_frame
	await physics_frame
	var meshes := _held_meshes(world)
	_check(_mesh_count_of(meshes, "Sword") == 1, "exactly one sword mesh appears")
	var sword_mesh = _mesh_for(meshes, "Sword")
	if sword_mesh != null:
		_check(int(sword_mesh.get_meta("item_id", 0)) == _id_of_model(sheet, "Sword"),
				"and the mesh carries the instance id the snapshot names, not the definition")
		_check(_socket_of(sword_mesh) == "RightHandSocket",
				"and hangs from the hand C++ says holds it")

	# The mesh follows the hand: the same item, now in the other one.
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "the sword comes off")
	await physics_frame
	await physics_frame
	_check(_mesh_count_of(_held_meshes(world), "Sword") == 0,
			"and unequipping removes the mesh on the next tick")
	_check(sheet.equip_item(_id_of_model(sheet, "Sword"), SLOT_LEFT_HAND), "the sword goes to the left")
	await physics_frame
	await physics_frame
	meshes = _held_meshes(world)
	_check(_mesh_count_of(meshes, "Sword") == 1, "one sword mesh again, not two")
	var moved = _mesh_for(meshes, "Sword")
	if moved != null:
		_check(_socket_of(moved) == "LeftHandSocket",
				"and the mesh moved with the item: it hangs from the left hand now")

	# One mesh per item, even when a two-hander reserves both hands.
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "sword off again")
	_check(sheet.equip_item(_id_of_model(sheet, "Spear"), SLOT_LEFT_HAND),
			"the spear is taken in hand")
	await physics_frame
	await physics_frame
	meshes = _held_meshes(world)
	_check(_mesh_count_of(meshes, "Spear") == 1,
			"the two-handed spear is one mesh, not one per hand")
	var spear_slots: Array = _row_of(sheet, _id_of_model(sheet, "Spear")).get("slots", [])
	_check(spear_slots == [SLOT_LEFT_HAND, SLOT_RIGHT_HAND], "while C++ holds both hands for it")

	# A hand the spear already took is not a hand a dagger can join.
	_check(not sheet.equip_item(_id_of_model(sheet, "Dagger"), SLOT_RIGHT_HAND),
			"the dagger is refused while the spear holds both hands")
	await physics_frame
	await physics_frame
	meshes = _held_meshes(world)
	_check(_mesh_count_of(meshes, "Dagger") == 0,
			"a dagger cannot join a hand the spear already took, so no mesh either")

	# More ticks and a rebinding must not stack duplicates of what is already drawn.
	for frame in range(4):
		await physics_frame
	var actors: Array = world.get("_actors")
	feedback.call("bind_combat", world.get("_combat"), actors)
	await physics_frame
	await physics_frame
	meshes = _held_meshes(world)
	_check(_mesh_count_of(meshes, "Spear") == 1, "no duplicate mesh after more ticks and a rebind")
	_check(_held_meshes(world).size() == 1, "and nothing else was attached alongside it")

	# The rest of the kit, one hand each: a one-handed item follows the slot it is given.
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "the spear comes back off")
	for model in ["Dagger", "Hammer_Small", "Shield_Round"]:
		var item := _id_of_model(sheet, String(model))
		_check(sheet.equip_item(item, SLOT_RIGHT_HAND), "the %s is held" % String(model))
		await physics_frame
		await physics_frame
		var held := _held_meshes(world)
		_check(_mesh_count_of(held, String(model)) == 1, "and the %s shows in hand" % String(model))
		var mesh = _mesh_for(held, String(model))
		if mesh != null:
			_check(_socket_of(mesh) == "RightHandSocket",
					"the %s hangs from the right hand" % String(model))
		_check(sheet.unequip_item(SLOT_RIGHT_HAND), "off again")
		await physics_frame
		await physics_frame
		_check(_mesh_count_of(_held_meshes(world), String(model)) == 0,
				"and the %s mesh goes with it" % String(model))
	await _free(world)


func _a_dead_limb_keeps_its_mesh() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	var player = world.get_node("Player")
	var sheet = player.get("sheet")
	_check(sheet.equip_item(_id_of_model(sheet, "Sword"), SLOT_RIGHT_HAND),
			"the sword is held in the right hand")
	await physics_frame
	await physics_frame
	_check(_mesh_count_of(_held_meshes(world), "Sword") == 1, "and shows there")

	_check(sheet.apply_damage(PART_RIGHT_ARM, sheet.get_part_max_health(PART_RIGHT_ARM)),
			"the right arm is spent")
	_check(sheet.is_alive(), "and the sheet survives it")
	await physics_frame
	await physics_frame
	_check(_mesh_count_of(_held_meshes(world), "Sword") == 1,
			"a disabled limb does not delete the equipped item or its mesh")
	var row := _row_of(sheet, _id_of_model(sheet, "Sword"))
	_check(row.get("slots", []) == [SLOT_RIGHT_HAND], "the sheet still says it is held there")
	_check(int(feedback.call("swing_motion_count", sheet.get_character_id())) == 0,
			"and nothing swung, so nothing moved: motion follows the simulation's reports")
	await _free(world)


func _the_dummy_view_needs_no_orders() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	var combat = world.get("_combat")
	var enemy = world.get_node("Enemy")
	var enemy_sheet = enemy.get("sheet")
	var enemy_start: Vector3 = enemy.global_position
	_check(combat.setup_starter_player(enemy_sheet.get_character_id()),
			"the production setup command can equip a dummy too")
	var sword := _id_of_model(enemy_sheet, "Sword")
	_check(enemy_sheet.equip_item(sword, SLOT_RIGHT_HAND), "and its own sheet can hold the sword")
	await physics_frame
	await physics_frame
	var meshes := _held_meshes(world)
	_check(_mesh_count_of(meshes, "Sword") == 1, "the held sword is drawn for the dummy view")
	var mesh = _mesh_for(meshes, "Sword")
	if mesh != null:
		var actor_root: Node = mesh
		while actor_root != null and actor_root != enemy:
			actor_root = actor_root.get_parent()
		_check(actor_root == enemy, "on the dummy's own body, not the player's")
	_check(_id_of_model(_sheet_of(world, "Player"), "Sword") != 0
			and _mesh_count_of(meshes, "Sword") == 1,
			"one sword mesh: the dummy's, since the player holds nothing")
	_check(_close(enemy.global_position.x - enemy_start.x, 0.0) \
			and _close(enemy.global_position.z - enemy_start.z, 0.0),
			"and drawing the dummy cost nobody an order: it has not walked anywhere")
	_check(int(enemy_sheet.get_attack_target_id()) == 0, "nor was it ever ordered to attack")
	await _free(world)


# --- one performed swing, one motion -------------------------------------------

func _one_performed_swing_one_motion() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	var player = world.get_node("Player")
	var ally = world.get_node("Friendly")
	var enemy = world.get_node("Enemy")
	var combat = world.get("_combat")
	var player_id: int = player.get("sheet").get_character_id()
	var ally_id: int = ally.get("sheet").get_character_id()

	# Stand the fight up inside reach, then stop every clock the scene owns so each swing
	# below is one deliberate manual tick and nothing else.
	ally.global_position = Vector3(-4.0, 0.1, -2.0)
	player.global_position = Vector3(-2.7, 0.1, -2.0)
	await physics_frame
	await physics_frame
	for node in [world, player, ally, enemy, feedback]:
		node.set_physics_process(false)

	_check(combat.issue_attack(player_id, ally_id), "the attack order is accepted")
	_tick(world, 0.1)
	_tick(feedback, 0.1)
	_check(int(feedback.call("swing_motion_count", player_id)) == 1,
			"the performed opening swing plays exactly one motion")
	_check(_labels_of(feedback).is_empty(),
			"a turned-aside blow reports zero damage, and shows no number")

	# The same reports, read again: draining is the only clearing, so an idle poll replays
	# nothing.
	for frame in range(4):
		_tick(feedback, 0.1)
	_check(int(feedback.call("swing_motion_count", player_id)) == 1,
			"polling again plays no second motion for the same swing")
	_check(_labels_of(feedback).is_empty(), "and conjures no number either")

	# A refused attempt - the new target is out of reach and nothing can walk while the
	# scene's clocks are stopped - produces no event, so the animation layer stays silent.
	_check(combat.issue_attack(player_id, enemy.get("sheet").get_character_id()),
			"the order at the distant Enemy is accepted")
	for frame in range(6):
		_tick(world, 0.1)
		_tick(feedback, 0.1)
	_check(int(feedback.call("swing_motion_count", player_id)) == 1,
			"a swing nobody could perform plays no motion")
	_check(_labels_of(feedback).is_empty(), "and no damage either")

	# The defender was never ordered and is never animated as if it were.
	_check(int(feedback.call("swing_motion_count", ally_id)) == 0,
			"the sheet that only defended plays no attack motion")
	await _free(world)


func _landed_damage_shows_the_number_that_arrived() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	var player = world.get_node("Player")
	var ally = world.get_node("Friendly")
	var enemy = world.get_node("Enemy")
	var combat = world.get("_combat")
	var enemy_sheet = enemy.get("sheet")
	var player_id: int = player.get("sheet").get_character_id()
	var ally_id: int = ally.get("sheet").get_character_id()
	var enemy_id: int = enemy_sheet.get_character_id()

	# Two attackers, one target: the answer one spends leaves the other's blow unanswered,
	# which is how a whole typed blow lands with nothing but the scene clock driving it.
	ally.global_position = enemy.global_position + Vector3(0.0, 0.0, 0.9)
	player.global_position = enemy.global_position + Vector3(0.9, 0.0, 0.0)
	await physics_frame
	await physics_frame
	for node in [world, player, ally, enemy, feedback]:
		node.set_physics_process(false)
	_check(combat.issue_attack(player_id, enemy_id), "the player is ordered at the Enemy")
	_check(combat.issue_attack(ally_id, enemy_id), "and so is the Friendly")

	var guard := 0
	while guard < 400 and _labels_of(feedback).is_empty():
		_tick(world, 0.1)
		_tick(feedback, 0.1)
		guard += 1
	_check(guard < 400, "a landed blow showed a number within 400 manual ticks")
	if guard >= 400:
		await _free(world)
		return

	# Keep ticking a little: with two attackers a few more blows must arrive, and the screen
	# has to keep pace with the sheet - one label per arriving blow, the sum of the labels
	# equal to the HP that actually left.
	for frame in range(120):
		_tick(world, 0.1)
		_tick(feedback, 0.1)
	var labels := _labels_of(feedback)
	_check(labels.size() >= 2, "more blows landed and each got its own number, got %d"
			% labels.size())
	var shown := 0.0
	for label in labels:
		var value := float(String(label.text))
		_check(value > 0.0, "a shown number reports damage that arrived: %s" % String(label.text))
		_check(_close(value, TYPED_BLOW, 0.0001),
				"the number is the typed blow C++ applied, not one the visual code chose: %s"
				% String(label.text))
		shown += value
	_check(_close(100.0 - float(enemy_sheet.get_health()), shown, 0.0005),
			"the sum of the numbers equals the HP the sheet lost, so nothing applied it twice")
	var motions := int(feedback.call("swing_motion_count", player_id)) \
		+ int(feedback.call("swing_motion_count", ally_id))
	_check(motions >= labels.size(), "every swing that landed also moved, from either attacker")
	_check(int(feedback.call("swing_motion_count", player_id)) >= 1
			and int(feedback.call("swing_motion_count", ally_id)) >= 1,
			"and both attacker views animated: the motion follows the sheet that swung")
	_check(int(enemy_sheet.get_attack_target_id()) == 0,
			"while the target still never ordered anything")

	# Drained, then polled: the reports already drawn cannot be drawn again.
	var labels_seen := labels.size()
	var motions_seen := motions
	for frame in range(5):
		_tick(feedback, 0.1)
	_check(_labels_of(feedback).size() == labels_seen, "an idle poll adds no label")
	_check(int(feedback.call("swing_motion_count", player_id)) \
			+ int(feedback.call("swing_motion_count", ally_id)) == motions_seen,
			"and no motion")
	await _free(world)


func _tiny_and_technique_events_stay_legible() -> void:
	var world = await _world()
	var feedback = _feedback_of(world)
	if feedback == null:
		await _free(world)
		return
	# Reports are handed to the consumer directly here, because producing a 0.001 hit or a
	# technique blow on demand would mean rigging the simulation from the scene - the one
	# thing this layer is not allowed to do. C++ decides what a swing is worth; these are the
	# formats it can produce, taken from its own reported shape.
	var small := {
		"actor_id": 1, "target_id": 3, "part": 1, "damage": 0.001, "prevented": 0.0,
		"nullified": false, "technique_id": 201, "attacker_item": 0, "defender_item": 0,
		"attack_cooldown": 0.5,
	}
	var nothing := {
		"actor_id": 1, "target_id": 3, "part": 0, "damage": 0.0, "prevented": 0.0,
		"nullified": true, "technique_id": 202, "attacker_item": 0, "defender_item": 0,
		"attack_cooldown": 0.5,
	}
	var big := {
		"actor_id": 2, "target_id": 3, "part": 1, "damage": 900.0, "prevented": 0.0,
		"nullified": false, "technique_id": 0, "attacker_item": 0, "defender_item": 0,
		"attack_cooldown": 2.0,
	}
	feedback.call("_play_reports", [small])
	var labels := _labels_of(feedback)
	_check(labels.size() == 1, "a performed technique blow is one motion, one number")
	_check(int(feedback.call("swing_motion_count", 1)) == 1,
			"and a technique event animates like the basic one it is shaped as")
	if labels.size() == 1:
		_check(String(labels[0].text) != "0" and float(String(labels[0].text)) > 0.0,
				"a thousandth of damage is not displayed as nothing: got %s"
				% String(labels[0].text))
	feedback.call("_play_reports", [nothing])
	_check(_labels_of(feedback).size() == 1, "a zero-damage event is not falsely positive")
	_check(int(feedback.call("swing_motion_count", 1)) == 2,
			"while the swing it reports is still performed, and still moves")
	feedback.call("_play_reports", [big])
	labels = _labels_of(feedback)
	_check(labels.size() == 2, "and a big hit gets its own number too")
	if labels.size() == 2:
		var small_size: int = int(labels[0].font_size)
		var big_size: int = int(labels[1].font_size)
		_check(big_size > small_size,
				"bigger damage is drawn bigger: %d against %d" % [big_size, small_size])
		_check(big_size <= 52 and small_size >= 22,
				"and both extremes stay on the screen: %d and %d" % [big_size, small_size])
	# And a chip so small that five fixed decimals would print zero: C++ said something
	# arrived, and the screen must not answer "0". Chained fractions - an exhausted half
	# dodge, then armour, then a zero-durability third - can reach this low.
	var chip := {
		"actor_id": 1, "target_id": 3, "part": 1, "damage": 0.00000001, "prevented": 0.0,
		"nullified": false, "technique_id": 0, "attacker_item": 0, "defender_item": 0,
		"attack_cooldown": 0.5,
	}
	feedback.call("_play_reports", [chip])
	labels = _labels_of(feedback)
	_check(labels.size() == 3, "a positive chip is a number on the screen")
	if labels.size() == 3:
		_check(String(labels[2].text) != "0" and float(String(labels[2].text)) > 0.0,
				"ten ten-millionths of damage is not displayed as nothing: got %s"
				% String(labels[2].text))
	await _free(world)


func _two_weapons_of_one_model_hang_in_two_hands() -> void:
	var world: Node = await _world()
	var combat = world.get("_combat")
	var feedback = _feedback_of(world)
	var sheet = _sheet_of(world, "Player")
	if feedback == null:
		await _free(world)
		return
	# A crafted copy of the starter sword: same model, different instance - the case a
	# shelf that files held items by model cannot survive. The forge is in the scene, so
	# the copy is made the only honest way: paid for, at the bench, fifteen real seconds.
	world.set_physics_process(false)
	var bench_node: Node3D = world.find_children("SmithingWorkbench", "", true, false)[0]
	var player: Node = world.get_node("Player")
	player.global_position = bench_node.global_position + Vector3(0.8, 0.0, 0.0)
	combat.set_position(1, player.global_position)
	_check(combat.request_work(1, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "the forge takes the order")
	for second in range(16):
		combat.step(1.0)
	_check(String(combat.get_work_snapshot(1).get("phase")) == "completed",
			"the forge delivered a second sword")
	var ids := []
	for row in sheet.get_equipment_snapshot():
		if String(row["model_key"]) == "Sword":
			ids.append(int(row["item_id"]))
	_check(ids.size() == 2, "one model, two instances: %s" % str(ids))
	if ids.size() != 2:
		world.set_physics_process(true)
		await _free(world)
		return
	_check(sheet.equip_item(int(ids[0]), SLOT_RIGHT_HAND), "the starter sword goes right")
	_tick(feedback, 0.1)
	var meshes := _held_meshes(world)
	_check(_mesh_count_of(meshes, "Sword") == 1, "one sword, one mesh")
	_check(sheet.equip_item(int(ids[1]), SLOT_LEFT_HAND), "the forged sword goes left")
	_tick(feedback, 0.1)
	meshes = _held_meshes(world)
	_check(_mesh_count_of(meshes, "Sword") == 2, "two swords of one model, two meshes")
	var sockets := []
	for mesh in _held_meshes(world):
		if String(mesh.get_meta("model_key", "")) == "Sword":
			sockets.append(_socket_of(mesh))
	sockets.sort()
	_check(sockets == ["LeftHandSocket", "RightHandSocket"], "one hangs in each hand")
	# And each mesh belongs to the instance it was hung for: taking the forged sword off
	# must not take the starter sword's mesh with it.
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "the forged sword comes off")
	_tick(feedback, 0.1)
	_check(_mesh_count_of(_held_meshes(world), "Sword") == 1,
			"the sword still held keeps its mesh")
	world.set_physics_process(true)
	await _free(world)


func _the_swing_plays_the_clip_c_asks_for() -> void:
	var world: Node = await _world()
	var feedback = _feedback_of(world)
	var sheet = _sheet_of(world, "Player")
	var player_node: Node = world.get_node("Player")
	if feedback == null:
		await _free(world)
		return
	world.set_physics_process(false)
	var players: Array = player_node.find_children("CharacterAnimationPlayer", "AnimationPlayer",
			true, false)
	_check(players.size() == 1, "the player model has one animation player")
	if players.size() != 1:
		world.set_physics_process(true)
		await _free(world)
		return
	var anim: AnimationPlayer = players[0]

	# The stance follows the held, usable thing.
	var sword := _id_of_model(sheet, "Sword")
	_check(sheet.equip_item(sword, SLOT_RIGHT_HAND), "the sword is held")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Sword_Idle", "a usable weapon stands the armed way")
	_check(is_zero_approx(player_node.get_node("Visual").rotation_degrees.x),
			"and the body itself no longer tilts to swing")

	# A heavy event on this actor starts the heavy clip at the event's own duration.
	var heavy := {"actor_id": 1, "target_id": 3, "part": 1, "damage": 0.5, "prevented": 0.0,
			"nullified": false, "technique_id": 3, "attacker_item": sword, "defender_item": 0,
			"attack_cooldown": 2.0}
	feedback.call("_play_reports", [heavy])
	_check(anim.current_animation == &"Sword_Heavy_Combo", "the heavy event plays the heavy clip")
	var clip_length: float = anim.get_animation(&"Sword_Heavy_Combo").length
	_check(absf(anim.speed_scale - clip_length / 2.0) < 0.0001, "at the event's own two seconds")
	_check(int(feedback.swing_motion_count(1)) == 1, "and that is exactly one motion")

	# What swings is the event's attacker_item, not whatever the equipment says now.
	var bare: Dictionary = heavy.duplicate()
	bare["attacker_item"] = 0
	bare["technique_id"] = 0
	feedback.call("_play_reports", [bare])
	_check(anim.current_animation == &"Punch_Jab", "an unarmed event jabs with the sword still held")
	_check(int(feedback.swing_motion_count(1)) == 2, "the second event is the second motion")

	# Let the jab finish - stances are only rewritten between attacks.
	for step in range(100):
		if anim.current_animation != &"Punch_Jab":
			break
		anim.advance(0.2)
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "sword off")
	_check(sheet.equip_item(_id_of_model(sheet, "Shield_Round"), SLOT_LEFT_HAND), "shield up")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Idle_Shield", "shield-only stands the shielded way")
	_check(sheet.equip_item(sword, SLOT_RIGHT_HAND), "the sword goes back up")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Sword_Idle", "a usable weapon wins the stance")

	# Nothing held: the exported idle. The two-hander with one dead arm falls back to it
	# too - and keeps its mesh, which is a limb fact, not an inventory one.
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "sword off again")
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "shield off too")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Idle_FoldArms", "nothing held stands down")
	_check(sheet.equip_item(_id_of_model(sheet, "Spear"), SLOT_LEFT_HAND), "the spear is gripped")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Sword_Idle", "the two-hander counts as a weapon held")
	_check(sheet.apply_damage(2, 1.0e30), "the left arm falls")
	feedback.call("_physics_process", 0.1)
	_check(anim.current_animation == &"Idle_FoldArms", "the unusable two-hander falls back down")
	var spears := 0
	for mesh in _held_meshes(world):
		if String(mesh.get_meta("model_key", "")) == "Spear":
			spears += 1
	_check(spears == 1, "and the disabled arm keeps the spear's mesh")
	world.set_physics_process(true)
	await _free(world)


func _run() -> void:
	await _the_world_owns_a_feedback_node()
	await _every_starter_item_shows_in_the_hand_that_holds_it()
	await _meshes_hang_from_the_hand_bones_themselves()
	await _two_weapons_of_one_model_hang_in_two_hands()
	await _a_dead_limb_keeps_its_mesh()
	await _the_dummy_view_needs_no_orders()
	await _one_performed_swing_one_motion()
	await _the_swing_plays_the_clip_c_asks_for()
	await _landed_damage_shows_the_number_that_arrived()
	await _tiny_and_technique_events_stay_legible()
	_check(checks > 50, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Combat feedback: all checks passed")
	quit(1 if failures else 0)
