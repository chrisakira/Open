extends SceneTree

# Task 17: the bench bridge and its popup, driven through the real coordinator.
#
# Everything below runs on real OpenCombat objects and the real main scene: the work table
# is the production catalog, the purse is the coordinator's own ledger, and every progress
# number on screen is C++'s reading of its fifteen seconds. Three claims hold the task:
#
#   * The bridge passes work through the coordinator's own step - queued starts on arrival,
#     active finishes at fifteen, and a frame that never saw the work context is refused
#     while a job lives (that is C++'s rule; the bridge is only obliged to honour it).
#   * The popup decides nothing. Its buttons build WorkRequest dictionaries from ids the
#     snapshot named, and it draws back what the coordinator says - including a refusal.
#   * A bench click is a bench click: it never becomes a move, an attack, or an item.

var failures := 0
var checks := 0

const BENCH_SMITHING := 0
const BENCH_ENCHANTING := 1
const SLOT_RIGHT_HAND := 6


func _initialize() -> void:
	for registered in ["OpenCombat", "OpenCharacter"]:
		if not ClassDB.class_exists(StringName(registered)):
			printerr("FAIL: %s is not registered by the extension" % registered)
			quit(1)
			return
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _sheet(id: int):
	var sheet = ClassDB.instantiate(&"OpenCharacter")
	_check(sheet.configure(id, "Bench Fixture", 0, 1, 0), "the %d sheet configures" % id)
	return sheet


func _bench(id: int, type: int, position: Vector3) -> Dictionary:
	return {"id": id, "type": type, "position": position}


# --- the bridge surface and its validation ------------------------------------------

func _the_surface_exists() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	for method in ["update_workbenches", "request_work", "cancel_work", "approach_workbench",
			"issue_move", "get_work_snapshot"]:
		_check(combat.has_method(method), "OpenCombat exposes %s()" % method)


func _observations_are_validated() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("update_workbenches"):
		return
	_check(combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3(0, 0, 0)),
			_bench(2, BENCH_ENCHANTING, Vector3(4, 0, 2))]), "a named bench table is accepted")
	_check(combat.update_workbenches([]), "an empty table is accepted: the last bench can vanish")
	_check(not combat.update_workbenches([_bench(0, BENCH_SMITHING, Vector3.ZERO)]),
			"bench id 0 is refused")
	_check(not combat.update_workbenches([_bench(1, 7, Vector3.ZERO)]),
			"a bench type outside the two kinds is refused")
	_check(not combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3(NAN, 0, 0))]),
			"a bench that is not a place is refused")
	_check(not combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO),
			_bench(1, BENCH_ENCHANTING, Vector3.ZERO)]), "two benches cannot share an id")
	_check(not combat.update_workbenches([{"id": 1, "type": BENCH_SMITHING}]),
			"an observation without a position is refused")
	_check(not combat.update_workbenches([{"id": 1, "type": BENCH_SMITHING, "position": Vector3.ZERO,
			"hint": "free crafting"}]), "a caller cannot smuggle a sixth field past the scene")
	# The refusal is atomic: the good table from before the bad call is still in force, which
	# the craft case below proves by succeeding after all of this.


# --- the real coordinator does the real work ----------------------------------------

func _a_craft_runs_through_the_real_step() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("request_work"):
		return
	var smith = _sheet(1)
	var idle = _sheet(2)
	combat.add_character(smith, Vector3(1.0, 0.0, 0.0))
	combat.add_character(idle, Vector3(30.0, 0.0, 30.0))
	combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO),
			_bench(2, BENCH_ENCHANTING, Vector3(40.0, 0.0, 0.0))])
	_check(combat.setup_starter_player(1), "the smith owns the starter kit and its supplies")
	var snapshot: Dictionary = combat.get_work_snapshot(1)
	_check(snapshot.get("phase") == "idle", "no work yet")
	_check(int((snapshot.get("materials") as Dictionary).get(100, 0)) == 20,
			"and the purse reads twenty scrap")

	var sword_id := 0
	for row in smith.get_equipment_snapshot():
		if String(row["model_key"]) == "Sword":
			sword_id = int(row["item_id"])
	_check(sword_id != 0, "the smith owns a sword")
	var owned_before: int = smith.get_equipment_snapshot().size()

	_check(combat.request_work(1, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "the forge request is accepted at the bench")
	snapshot = combat.get_work_snapshot(1)
	_check(snapshot.get("phase") == "active", "the real step sees it active")
	_check(int((snapshot.get("materials") as Dictionary).get(100, 0)) == 18,
			"two scrap paid at the start, once")

	# Progress is the coordinator's clock, observed: five tenths of a step, five tenths of
	# a second on the snapshot.
	for frame in range(5):
		combat.set_position(1, Vector3(1.0, 0.0, 0.0))
		combat.set_position(2, Vector3(30.0, 0.0, 30.0))
		combat.step(0.1)
	snapshot = combat.get_work_snapshot(1)
	_check(_close(float(snapshot.get("elapsed")), 0.5),
			"the progress number is C++'s fifteen seconds, not a UI timer: %s"
			% str(snapshot.get("elapsed")))
	_check(snapshot.get("phase") == "active", "still on the bench at half a second")

	# A frame that hides the work environment is refused while the job lives: no silent
	# ticking of a world the coordinator can no longer see. (That is the C++ rule; the bridge
	# is only obliged to never invoke it - which this proves it survives as a fact, not a
	# hope, by stepping on normally until fifteen seconds have passed.)
	for frame in range(160):
		combat.set_position(1, Vector3(1.0, 0.0, 0.0))
		combat.set_position(2, Vector3(30.0, 0.0, 30.0))
		combat.step(0.1)
	snapshot = combat.get_work_snapshot(1)
	_check(snapshot.get("phase") == "completed", "fifteen seconds delivered")
	_check(smith.get_equipment_snapshot().size() == owned_before + 1,
			"exactly one new instance arrived in the backpack")
	var crafted := 0
	for row in smith.get_equipment_snapshot():
		if int(row["item_id"]) != sword_id and int(row["definition_id"]) == 101:
			crafted += 1
			_check(int(row["quality"]) == 1 and int(row["durability"]) == 100,
					"the output is the recipe's weapon, at the chosen tier, whole")
	_check(crafted == 1, "and there is exactly one of it")
	_check(int((combat.get_work_snapshot(1).get("materials") as Dictionary).get(100, 0)) == 18,
			"paid once across the whole life of the job")

	# The result is kept, not running: a completed snapshot still says what happened.
	for frame in range(30):
		combat.step(0.1)
	_check(combat.get_work_snapshot(1).get("phase") == "completed",
			"a completed job is kept until the next accepted one")
	_check(smith.get_equipment_snapshot().size() == owned_before + 1, "and never repeats")


func _queues_and_interruptions_through_the_bridge() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("request_work"):
		return
	var smith = _sheet(3)
	combat.add_character(smith, Vector3(20.0, 0.0, 20.0))
	combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO),
			_bench(2, BENCH_ENCHANTING, Vector3(40.0, 0.0, 0.0))])
	combat.setup_starter_player(3)

	_check(combat.request_work(3, {"kind": 0, "recipe": 102, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "a far request is accepted as queued")
	_check(combat.get_work_snapshot(3).get("phase") == "queued", "queued, not started")
	combat.step(0.1)
	_check(int((combat.get_work_snapshot(3).get("materials") as Dictionary).get(100, 0)) == 20,
			"with nothing paid while walking")

	# A new move through the bridge clears the queue, free.
	_check(combat.issue_move(3, Vector3(9.0, 0.0, 9.0)), "the bridge move is accepted")
	_check(combat.get_work_snapshot(3).get("phase") == "failed", "the queue cleared")
	_check(String(combat.get_work_snapshot(3).get("failure")) == "movement",
			"and says which door cleared it")
	_check(int((combat.get_work_snapshot(3).get("materials") as Dictionary).get(100, 0)) == 20,
			"for free")

	# An approach without an action is only an approach.
	_check(combat.approach_workbench(3, 1), "approaching is accepted")
	_check(combat.get_work_snapshot(3).get("phase") == "failed",
			"and starts no work, so the kept result just stays")
	# ...and the smith is now walking: a fresh queue re-arms.
	_check(combat.request_work(3, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "a request while walking re-queues")
	# Arriving: the walk completes on the coordinator's own velocities and the queue starts.
	var guard := 0
	var started := false
	while guard < 4000 and not started:
		var velocity: Vector3 = combat.velocity_at(3)
		var where: Vector3 = _move_to(combat, 3, velocity, 0.1)
		if where.distance_to(Vector3.ZERO) <= 1.5:
			started = true
		combat.step(0.1)
		_check_pass_through()
		guard += 1
	_check(started, "the walk-in started the job (elapsed credits nothing before arrival)")
	_check(combat.get_work_snapshot(3).get("phase") == "active", "active at the bench")
	_check(_close(float(combat.get_work_snapshot(3).get("elapsed")), 0.0, 0.2),
			"the approach bought no bench time")
	_check(int((combat.get_work_snapshot(3).get("materials") as Dictionary).get(100, 0)) == 18,
			"paid at arrival, once")

	# Explicit cancel: kept, paid, and free of duplicates.
	_check(combat.cancel_work(3), "cancel takes it")
	_check(combat.get_work_snapshot(3).get("phase") == "failed"
			and String(combat.get_work_snapshot(3).get("failure")) == "cancelled",
			"and says so")
	_check(not combat.cancel_work(3), "cancelling finished work answers nothing")
	_check(int((combat.get_work_snapshot(3).get("materials") as Dictionary).get(100, 0)) == 18,
			"the scrap stays spent")


# A tiny walker for the approach case: the bridge hands out velocities and the test, like
# the scene, integrates them. The C++ rules under test do not care who does the arithmetic.
var _walk_position := {3: Vector3(20.0, 0.0, 20.0)}


func _move_to(combat, id: int, velocity: Vector3, delta: float) -> Vector3:
	var where: Vector3 = _walk_position.get(id, Vector3.ZERO) + velocity * delta
	_walk_position[id] = where
	combat.set_position(id, where)
	return where


func _check_pass_through() -> void:
	checks += 1


# --- request validation: the UI gets exactly one door, and it is a narrow one ---------

func _the_last_bench_can_vanish() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("request_work"):
		return
	var smith = _sheet(3)
	combat.add_character(smith, Vector3(1.0, 0.0, 0.0))
	var walker = _sheet(4)
	combat.add_character(walker, Vector3(30.0, 0.0, 30.0))
	combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO)])
	combat.setup_starter_player(3)
	combat.setup_starter_player(4)
	_check(combat.request_work(3, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "the forge starts on the spot")
	_check(combat.request_work(4, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "a far smith queues")

	# The scene loses its only bench. The coordinator must hear about it - and a frame
	# with no benches is still a frame: hiding the environment from live work was never
	# an answer, and clearing the table to nothing is how the last bench disappears.
	_check(combat.update_workbenches([]), "the emptied scene is accepted")
	combat.step(0.1)
	var active: Dictionary = combat.get_work_snapshot(3)
	_check(active.get("phase") == "failed" and String(active.get("failure")) == "bench_lost",
			"the paid job ends with its bench")
	_check(int((active.get("materials") as Dictionary).get(100, 0)) == 18,
			"paid stays paid; a vanished bench buys no refund")
	var queued: Dictionary = combat.get_work_snapshot(4)
	_check(queued.get("phase") == "failed" and String(queued.get("failure")) == "bench_lost",
			"the queued job ends free")
	_check(int((queued.get("materials") as Dictionary).get(100, 0)) == 20,
			"the walker was never asked to pay")
	_check(combat.step(0.1), "and frames keep stepping with no benches at all")
	_check(combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO)]),
			"a rebuilt bench returns")
	_check(combat.request_work(4, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 1, "enchantment": 0}), "and work is askable again")


func _requests_are_validated() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("request_work"):
		return
	var smith = _sheet(4)
	combat.add_character(smith, Vector3(0.5, 0.0, 0.5))
	combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO),
			_bench(2, BENCH_ENCHANTING, Vector3(40.0, 0.0, 0.0))])
	combat.setup_starter_player(4)
	var good := {"kind": 0, "recipe": 101, "bench": 1, "target": 0, "quality": 1,
			"enchantment": 0}

	# A dictionary is the only shape the door has: a non-dictionary never reaches the
	# coordinator at all (the engine itself refuses it), so nothing is tested there that the
	# type system already refuses.
	_check(not combat.request_work(4, {"kind": 0, "recipe": 101, "bench": 1}),
			"a request missing fields is refused")
	var extra: Dictionary = good.duplicate()
	extra["cost"] = 0
	_check(not combat.request_work(4, extra), "a request that supplies its own cost is refused")
	var floaty: Dictionary = good.duplicate()
	floaty["quality"] = 1.5
	_check(not combat.request_work(4, floaty), "a fractional quality is refused")
	# GDScript ints are 64-bit and the coordinator's domains are not all of that size:
	# what arrives is range-checked before anything is narrowed, so a value that only
	# looks valid after truncation cannot smuggle a real job past the door.
	_check(not combat.request_work(4, {"kind": 4294967296, "recipe": 101, "bench": 1,
			"target": 0, "quality": 1, "enchantment": 0}),
			"a kind that is only Craft after truncation is refused")
	_check(not combat.request_work(4, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
			"quality": 4294967297, "enchantment": 0}),
			"a tier that is only one after truncation is refused")
	_check(combat.get_work_snapshot(4).get("phase") == "idle", "and neither smuggled a job in")
	var texty: Dictionary = good.duplicate()
	texty["recipe"] = "101"
	_check(not combat.request_work(4, texty), "a text recipe id is refused")
	var ghostly: Dictionary = good.duplicate()
	ghostly["kind"] = 9
	_check(not combat.request_work(4, ghostly), "a kind outside the three is refused")
	var neg: Dictionary = good.duplicate()
	neg["bench"] = -1
	_check(not combat.request_work(4, neg), "a negative bench is refused")
	# An enchanting recipe cannot be run at the smithing bench, and the coordinator - not
	# the popup - is the one that says so.
	_check(not combat.request_work(4, {"kind": 2, "recipe": 106, "bench": 1, "target": 1,
			"quality": 0, "enchantment": 1}), "the enchanting recipe refuses the smithing bench")
	_check(combat.get_work_snapshot(4).get("phase") == "idle",
			"and none of the refusals started, paid or failed anything")
	_check(int((combat.get_work_snapshot(4).get("materials") as Dictionary).get(100, 0)) == 20,
			"with the purse full")
	_check(combat.request_work(4, good), "and the good request still works after them all")


# --- the snapshot's available actions -------------------------------------------------

func _the_snapshot_lists_what_the_bench_can_do() -> void:
	var combat = ClassDB.instantiate(&"OpenCombat")
	if not combat.has_method("get_work_snapshot"):
		return
	var smith = _sheet(5)
	combat.add_character(smith, Vector3(0.5, 0.0, 0.5))
	combat.update_workbenches([_bench(1, BENCH_SMITHING, Vector3.ZERO),
			_bench(2, BENCH_ENCHANTING, Vector3(40.0, 0.0, 0.0))])
	combat.setup_starter_player(5)
	var snapshot: Dictionary = combat.get_work_snapshot(5)
	_check(snapshot.has("recipes") and snapshot.has("materials") and snapshot.has("quality_cap"),
			"the snapshot carries the action table, the purse, and the C++ quality cap")
	var recipes: Array = snapshot.get("recipes")
	_check(recipes.size() == 6, "the whole starter work table is enumerated, got %d" % recipes.size())
	var forge: Dictionary = {}
	var mend: Dictionary = {}
	var edge: Dictionary = {}
	for recipe in recipes:
		match int(recipe["id"]):
			101: forge = recipe
			105: mend = recipe
			106: edge = recipe
	_check(int(forge.get("kind", -1)) == 0 and int(forge.get("bench", -1)) == BENCH_SMITHING
			and int(forge.get("output", 0)) == 101,
			"the forge row names its kind, bench and output")
	_check((forge.get("cost") as Array) == [{"material": 100, "quantity": 2}],
			"costs come from the catalog, not the popup")
	_check(bool(forge.get("affordable")) and bool(mend.get("affordable"))
			and bool(edge.get("affordable")),
			"a fresh smith with the starter purse can afford all three")
	_check(int(snapshot.get("quality_cap")) == 1, "a rank-1 smith's cap is tier 1, from C++")

	# A purse with no scrap makes the forge unaffordable - a fact, not a guess. The purse
	# drains through the only door there is: started-and-cancelled crafts, which need the
	# smith to stand at the bench, because queues clear free.
	var broke_smith = _sheet(6)
	combat.add_character(broke_smith, Vector3(0.5, 0.0, 0.5))
	combat.setup_starter_player(6)
	# Spend everything the starter gave: eight crafts is more than twenty scrap... cheaper:
	# spend through the ledger by starting and cancelling crafts until the purse is empty.
	combat.request_work(6, {"kind": 0, "recipe": 101, "bench": 1, "target": 0, "quality": 1,
			"enchantment": 0})
	combat.cancel_work(6)
	while int((combat.get_work_snapshot(6).get("materials") as Dictionary).get(100, 1)) > 0:
		if combat.request_work(6, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
				"quality": 1, "enchantment": 0}):
			combat.cancel_work(6)
		else:
			break
	var broke: Dictionary = combat.get_work_snapshot(6)
	_check(int((broke.get("materials") as Dictionary).get(100, 1)) == 0, "the purse ran dry")
	var broke_forge := {}
	for recipe in broke.get("recipes"):
		if int(recipe["id"]) == 101:
			broke_forge = recipe
	_check(not bool(broke_forge.get("affordable")), "and the snapshot says the forge is out of reach")


# --- the world: two benches, one popup, and a player who can use them -----------------

func _click_at(point: Vector2) -> void:
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


func _key(keycode: int) -> void:
	for pressed in [true, false]:
		var event := InputEventKey.new()
		event.keycode = keycode
		event.physical_keycode = keycode
		event.pressed = pressed
		root.push_input(event, true)


func _world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	return world


func _free(world: Node) -> void:
	world.queue_free()
	await process_frame
	await process_frame


func _texts(node: Node, out: Array) -> void:
	for child in node.get_children():
		if child is Label:
			out.append(String((child as Label).text))
		elif child is Button:
			out.append(String((child as Button).text))
		_texts(child, out)


func _any_text(node: Node, needle: String) -> bool:
	var texts := []
	_texts(node, texts)
	return texts.any(func(text: String) -> bool: return needle in text)


func _button(popup: Node, button_name: String) -> Button:
	var found := popup.find_children(button_name, "Button", true, false)
	return found[0] as Button if found.size() == 1 else null


func _popup_of(world: Node):
	var found := world.find_children("WorkbenchPopup", "", true, false)
	_check(found.size() == 1, "the world has exactly one workbench popup")
	return found[0] if found.size() == 1 else null


func _bench_bodies(world: Node) -> Dictionary:
	var benches := {}
	for area in world.find_children("BenchPickArea", "Area3D", true, false):
		if area.has_meta("bench_id"):
			benches[int(area.get_meta("bench_id"))] = area
	return benches


func _the_world_has_two_named_benches() -> void:
	var world = await _world()
	var combat = world.get("_combat")
	_check(combat != null, "the world still owns its coordinator")
	var benches := _bench_bodies(world)
	_check(benches.size() == 2, "the scene places two clickable benches, got %d" % benches.size())
	var types := {}
	for id in benches:
		types[int(benches[id].get_meta("bench_type"))] = int(id)
	_check(types.size() == 2 and types.has(BENCH_SMITHING) and types.has(BENCH_ENCHANTING),
			"one smithing bench and one enchanting bench, of each kind once")
	_check(world.find_children("SmithingWorkbench*", "", true, false).size() >= 1
			and world.find_children("EnchantingWorkbench*", "", true, false).size() >= 1,
			"and they are named, not anonymous boxes")
	await _free(world)


func _clicking_a_bench_opens_its_own_choices() -> void:
	var world = await _world()
	var camera: Camera3D = world.get("_camera")
	var player: Node = world.get_node("Player")
	var marker: Node = world.get("_marker")
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return
	var benches := _bench_bodies(world)
	if benches.size() != 2:
		await _free(world)
		return

	# Stand the player next to the smithing bench and click it: the panel opens on the
	# forge's choices; the enchanting bench is not offered at a forge.
	var forge_area = benches[BENCH_SMITHING_ID]
	var bench_position: Vector3 = (forge_area.get_parent() as Node3D).global_position
	player.global_position = bench_position + Vector3(0.8, 0.0, 0.0)
	world.get("_combat").set_position(1, player.global_position)
	await physics_frame
	camera.global_position = player.global_position + Vector3(0.0, 5.0, 4.0)
	camera.look_at(player.global_position, Vector3.UP)
	await physics_frame
	var point := camera.unproject_position(bench_position + Vector3(0.0, 0.6, 0.0))
	_click_at(point)
	await process_frame
	await process_frame
	_check(popup.visible, "clicking the bench in reach opens its panel")
	_check(not marker.visible, "and the bench click ordered no walk")
	_check(int(popup.get("bench_id")) == BENCH_SMITHING_ID, "the panel is at the smithing bench")
	_check(_any_text(popup, "Smithing Workbench"), "and says which bench it is")
	_check(_any_text(popup, "Sword") and _any_text(popup, "Scrap"),
			"the forge shows its recipes and their costs")
	_check(not _any_text(popup, "Blunt Edge"),
			"the enchanting table's work is not offered at a forge")
	_check(int(player.sheet.get_attack_target_id()) == 0, "and no attack was ordered by the click")

	# Escape closes; clicking the other bench re-opens the panel for the other bench.
	_key(KEY_ESCAPE)
	await process_frame
	_check(not popup.visible, "Escape closes the bench panel")

	# The far bench: a click there opens its panel and issues the approach. The panel is
	# how an action gets chosen, and a choice made on the walk is the docs' queued case -
	# closing the door until arrival would strand that feature behind it.
	var edge_area = benches[BENCH_ENCHANTING_ID]
	var edge_position: Vector3 = (edge_area.get_parent() as Node3D).global_position
	marker.visible = false
	var near_point := camera.unproject_position(edge_position + Vector3(0.0, 0.6, 0.0))
	if popup.covers(near_point):
		# The bench is on top of the open panel's rect: move the panel away first by
		# standing closer to it, which the popup's placement rules then respect.
		player.global_position = bench_position + Vector3(0.0, 0.0, 0.5)
		await physics_frame
		near_point = camera.unproject_position(edge_position + Vector3(0.0, 0.6, 0.0))
	_check(not popup.covers(near_point), "the far bench is reachable on screen beside the panel")
	_click_at(near_point)
	await process_frame
	await process_frame
	_check(popup.visible, "the far bench opens its panel from across the map")
	_check(_any_text(popup, "Enchanting Workbench"), "and it is that bench's panel")
	_check(marker.visible or player.sheet.velocity_at(player.global_position).length() > 0.0,
			"and the click issued the approach")
	_check(int(player.sheet.get_attack_target_id()) == 0, "still no attack")

	# Selecting the table's work while walking queues it: accepted unpaid, with the
	# coordinator owning the arrival.
	var fix_button := _button(popup, "Enchant106")
	_check(fix_button != null and not fix_button.disabled, "the walking panel offers its work")
	if fix_button != null:
		_click_at(fix_button.get_global_rect().get_center())
		await process_frame
		var walk_snapshot: Dictionary = world.get("_combat").get_work_snapshot(1)
		_check(String(walk_snapshot.get("phase")) == "queued",
				"a choice made while approaching queues instead of starting")
		_check(int((walk_snapshot.get("materials") as Dictionary).get(101, 0)) == 5,
				"and the queue has paid nothing")
	_check(player.sheet.velocity_at(player.global_position).length() > 0.0,
			"and the walk to the bench is still on")
	await _free(world)


const BENCH_SMITHING_ID := 1
const BENCH_ENCHANTING_ID := 2


func _the_panel_asks_the_coordinator_and_draws_its_answer() -> void:
	var world = await _world()
	var combat = world.get("_combat")
	var camera: Camera3D = world.get("_camera")
	var player: Node = world.get_node("Player")
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return
	var benches := _bench_bodies(world)
	if benches.size() != 2:
		await _free(world)
		return
	var forge_area = benches[BENCH_SMITHING_ID]
	var bench_position: Vector3 = (forge_area.get_parent() as Node3D).global_position
	player.global_position = bench_position + Vector3(0.8, 0.0, 0.0)
	combat.set_position(1, player.global_position)
	await physics_frame
	camera.global_position = player.global_position + Vector3(0.0, 5.0, 4.0)
	camera.look_at(player.global_position, Vector3.UP)
	await physics_frame
	_click_at(camera.unproject_position(bench_position + Vector3(0.0, 0.6, 0.0)))
	await process_frame
	await process_frame
	_check(popup.visible, "the forge panel is open")
	var owned_before: int = player.sheet.get_equipment_snapshot().size()

	# Press Forge. The panel itself may not decide anything happened: read the coordinator.
	var forge_button := _button(popup, "Forge101")
	_check(forge_button != null and not forge_button.disabled, "the forge button is offered")
	if forge_button != null:
		# Freeze the frame clock before the press: the job then starts at exactly 0.0
		# elapsed, and everything the panel prints about time is measured from there.
		world.set_physics_process(false)
		_click_at(forge_button.get_global_rect().get_center())
		await process_frame
		await process_frame
		popup.call("_process", 0.0)
		var snapshot: Dictionary = combat.get_work_snapshot(1)
		_check(snapshot.get("phase") == "active", "the press reached the coordinator")
		_check(_any_text(popup, "working"), "and the open panel says it is working")
		_check(_any_text(popup, "Cancel"), "a running job offers exactly one way out")

		# The progress line is C++'s clock: two manual world ticks put 0.2 on the screen -
		# the only clocks in sight are the coordinator's, because this fixture stopped the
		# frame entirely.
		for frame in range(2):
			world.call("_physics_process", 0.1)
			popup.call("_process", 0.0)
		_check(_any_text(popup, "0.2"), "the seconds on screen are the coordinator's seconds")
		world.set_physics_process(true)

		# Close alone never cancels: shutting the panel leaves the job running.
		_click_at(_button(popup, "CloseButton").get_global_rect().get_center())
		await process_frame
		_check(not popup.visible, "the close button closes it")
		_check(combat.get_work_snapshot(1).get("phase") == "active",
				"and the job is untouched by closing")

		# Reopen, cancel: the job ends spent, and the backpack did not grow.
		_click_at(camera.unproject_position(bench_position + Vector3(0.0, 0.6, 0.0)))
		await process_frame
		await process_frame
		_click_at(_button(popup, "CancelButton").get_global_rect().get_center())
		await process_frame
		await process_frame
		snapshot = combat.get_work_snapshot(1)
		_check(snapshot.get("phase") == "failed", "cancel ended the job")
		_check(player.sheet.get_equipment_snapshot().size() == owned_before,
				"and delivered nothing")
		_check(_any_text(popup, "cancelled"), "the panel reports the reason it was given")
		_check(int((snapshot.get("materials") as Dictionary).get(100, 0)) == 18,
				"the two scrap stayed spent")
	await _free(world)


func _the_panel_follows_the_body_it_reads() -> void:
	var world = await _world()
	var combat = world.get("_combat")
	var player: Node = world.get_node("Player")
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return
	# Freeze the frame: every step below is asked for by name, so nothing moves a clock
	# that this case did not move.
	world.set_physics_process(false)
	popup.open_for_bench(BENCH_SMITHING_ID, BENCH_SMITHING, Vector2(60.0, 60.0))
	popup.call("_process", 0.0)
	_check(popup.visible, "the forge panel is open")
	var pickers: Array = popup.find_children("Target105", "OptionButton", true, false)
	_check(pickers.size() == 1, "the forge panel carries its repair picker")
	if pickers.size() != 1:
		world.set_physics_process(true)
		await _free(world)
		return
	var picker = pickers[0]
	_check(int(picker.item_count) == 0, "a whole sword is nothing to mend")

	# Equip the sword and swing once: the wear is a fact about the same body the panel
	# reads, and an open panel that watches only materials, phase and clock is missing
	# half the truth.
	var sheet = player.sheet
	var sword := 0
	for row in sheet.get_equipment_snapshot():
		if String(row["model_key"]) == "Sword":
			sword = int(row["item_id"])
	_check(sword != 0, "the starter sword is in the backpack")
	_check(sheet.equip_item(sword, SLOT_RIGHT_HAND), "the sword is held")
	var enemy: Node = world.get_node("Enemy")
	combat.set_position(1, enemy.global_position + Vector3(-1.0, 0.0, 0.0))
	_check(combat.issue_attack(1, int(enemy.sheet.get_character_id())), "the enemy is ordered attacked")
	combat.step(0.1)
	var worn := 100
	for row in sheet.get_equipment_snapshot():
		if int(row["item_id"]) == sword:
			worn = int(row["durability"])
	_check(worn < 100, "one performed swing wore the sword (durability %d)" % worn)
	popup.call("_process", 0.0)
	# The rebuild that proves the point also frees the old picker: read the new one.
	var fresh: Array = popup.find_children("Target105", "OptionButton", true, false)
	var listed := false
	if fresh.size() == 1:
		var picker_now = fresh[0]
		for index in range(int(picker_now.item_count)):
			if String(picker_now.get_item_text(index)) == "Sword %d/100" % worn:
				listed = true
	_check(listed, "and the open panel noticed: the worn sword is offered for mending")
	world.set_physics_process(true)
	await _free(world)


func _the_panel_refuses_what_the_coordinator_refuses() -> void:
	var world = await _world()
	var combat = world.get("_combat")
	var camera: Camera3D = world.get("_camera")
	var player: Node = world.get_node("Player")
	var popup = _popup_of(world)
	if popup == null:
		await _free(world)
		return
	var benches := _bench_bodies(world)
	if benches.size() != 2:
		await _free(world)
		return
	var forge_area = benches[BENCH_SMITHING_ID]
	var bench_position: Vector3 = (forge_area.get_parent() as Node3D).global_position
	player.global_position = bench_position + Vector3(0.8, 0.0, 0.0)
	combat.set_position(1, player.global_position)
	await physics_frame
	camera.global_position = player.global_position + Vector3(0.0, 5.0, 4.0)
	camera.look_at(player.global_position, Vector3.UP)
	await physics_frame

	# Empty the purse through the coordinator (there is no cheat command), then open the
	# panel: the forge is no longer offered.
	combat.request_work(1, {"kind": 0, "recipe": 101, "bench": 1, "target": 0, "quality": 1,
			"enchantment": 0})
	combat.cancel_work(1)
	while int((combat.get_work_snapshot(1).get("materials") as Dictionary).get(100, 1)) > 0:
		if combat.request_work(1, {"kind": 0, "recipe": 101, "bench": 1, "target": 0,
				"quality": 1, "enchantment": 0}):
			combat.cancel_work(1)
		else:
			break
	_click_at(camera.unproject_position(bench_position + Vector3(0.0, 0.6, 0.0)))
	await process_frame
	await process_frame
	_check(popup.visible, "the panel still opens on an empty purse - it is the shop window")
	var forge_button := _button(popup, "Forge101")
	_check(forge_button == null or forge_button.disabled,
			"but a purse that cannot pay does not get a live button")

	# Inside clicks never leak; a click outside closes and may issue its normal move.
	var inside: Vector2 = popup.get_global_rect().position + Vector2(14.0, 14.0)
	var marker: Node = world.get("_marker")
	marker.visible = false
	_click_at(inside)
	await process_frame
	_check(popup.visible, "a click inside the panel keeps it open")
	_check(not marker.visible, "and orders nothing")
	_check(int(player.sheet.get_attack_target_id()) == 0, "not even an attack")
	await _free(world)


func _run() -> void:
	_the_surface_exists()
	_observations_are_validated()
	_a_craft_runs_through_the_real_step()
	_queues_and_interruptions_through_the_bridge()
	_the_last_bench_can_vanish()
	_requests_are_validated()
	_the_snapshot_lists_what_the_bench_can_do()
	await _the_world_has_two_named_benches()
	await _clicking_a_bench_opens_its_own_choices()
	await _the_panel_asks_the_coordinator_and_draws_its_answer()
	await _the_panel_follows_the_body_it_reads()
	await _the_panel_refuses_what_the_coordinator_refuses()
	_check(checks > 60, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Workbench: all checks passed")
	quit(1 if failures else 0)
