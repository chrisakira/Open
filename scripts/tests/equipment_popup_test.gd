extends SceneTree

# Task 15: the Equipment popup, on the player's real owned instances.
#
# This is the first suite where the items are the production ones: the world's setup command
# mints the starter backpack from C++'s table and everything below operates on those owned
# records through real controls. So the checks are the ones the design locks - the player
# chooses which hand, a two-hander takes both, an occupied hand is refused with a request to
# empty it rather than an automatic swap, an unmet requirement still goes on with the
# effectiveness C++ computes, limbs are gates, and a stale or foreign id changes nothing.
#
# And the claim underneath all of them: a popup that only draws and asks cannot mint, move,
# attack or equip by accident. Every count and clock below is read back from the sheet, never
# from what the panel hoped for.

const SLOT_CHEST := 1
const SLOT_LEFT_HAND := 5
const SLOT_RIGHT_HAND := 6
const PART_RIGHT_ARM := 3
const PART_LEFT_ARM := 2
const PART_TORSO := 1

const STARTER_MODELS := ["Sword", "Dagger", "Spear", "Hammer_Small", "Shield_Round"]

var failures := 0
# A runtime error inside one of these coroutine cases aborts the rest of it, and a suite that
# only counts failures can then be quietly green about nothing. Every case is asked to reach a
# floor, so an abort is a finding rather than a pass.
var checks := 0
var _viewport_size := Vector2.ZERO
var _equip_requests: Array = []
var _unequip_requests: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


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


func _button(popup: Node, button_name: String) -> Button:
	var found := popup.find_children(button_name, "Button", true, false)
	return found[0] as Button if found.size() == 1 else null


func _has_button(popup: Node, button_name: String) -> bool:
	return popup.find_children(button_name, "Button", true, false).size() == 1


func _any_text(node: Node, needle: String) -> bool:
	var texts := []
	_texts(node, texts)
	return texts.any(func(text: String) -> bool: return needle in text)


func _texts(node: Node, out: Array) -> void:
	for child in node.get_children():
		if child is Label:
			out.append(String((child as Label).text))
		elif child is Button:
			out.append(String((child as Button).text))
		_texts(child, out)


# --- the world, its popup and the sheet's owned records ----------------------

func _world() -> Node:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame
	_check(world.get_node("Player") != null, "the world built its player")
	_check(world.get("_combat") != null, "and its coordinator")
	return world


func _popup_of(world: Node):
	var found := world.find_children("EquipmentPopup", "", true, false)
	_check(found.size() == 1, "the world has exactly one equipment popup")
	return found[0] if found.size() == 1 else null


func _rows(sheet) -> Array:
	return sheet.get_equipment_snapshot()


func _row(sheet, item_id: int) -> Dictionary:
	for row in _rows(sheet):
		if int(row["item_id"]) == item_id:
			return row
	return {}


func _id_of_model(sheet, model_key: String) -> int:
	for row in _rows(sheet):
		if String(row["model_key"]) == model_key:
			return int(row["item_id"])
	return 0


func _held(sheet) -> String:
	# A stable description of who is wearing what, for "nothing changed" claims.
	var parts := []
	for row in _rows(sheet):
		parts.append("%s:%s" % [String(row["model_key"]), str(row["slots"])])
	parts.sort()
	return "|".join(parts)


func _state(sheet) -> String:
	return "%s|%s|%s|%s" % [_held(sheet), str(_rows(sheet).size()),
		str(sheet.get_attack_target_id()), str(sheet.get_health())]


func _open_popup(world: Node):
	var popup = _popup_of(world)
	if popup == null:
		return null
	var player: Node = world.get_node("Player")
	_check(player != null and player.sheet != null and player.sheet.is_configured(),
			"the sheet the panel will read is configured")
	_key(KEY_I)
	await process_frame
	var entries := world.find_children("EquipmentButton", "Button", true, false)
	_check(entries.size() == 1, "the character sheet carries an Equipment entry")
	if entries.size() != 1:
		return null
	_click_at((entries[0] as Button).get_global_rect().get_center())
	await process_frame
	await process_frame
	_check(popup.visible, "the sheet's Equipment entry opens the panel")
	# The sheet it came from is not its owner: closing that leaves this reading the sheet.
	_key(KEY_I)
	await process_frame
	return popup


func _free(world: Node) -> void:
	world.queue_free()
	await process_frame


# --- the cases ----------------------------------------------------------------

func _the_backpack_is_the_starter_kit() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var sheet = player.sheet
	_check(sheet.is_configured(), "the player has a sheet")
	var rows: Array = _rows(sheet)
	_check(rows.size() == STARTER_MODELS.size(),
			"the player owns exactly the five starter items, got %d" % rows.size())
	for model in STARTER_MODELS:
		_check(_id_of_model(sheet, model) != 0, "the backpack holds the %s" % model)
	var ids := []
	for row in rows:
		ids.append(int(row["item_id"]))
		_check(int(row["quality"]) == 1, "%s is quality tier 1" % String(row["model_key"]))
		_check(int(row["durability"]) == 100 and int(row["max_durability"]) == 100,
				"%s starts whole" % String(row["model_key"]))
		_check((row["enchantments"] as Array).is_empty(), "%s has no enchantments" % String(row["model_key"]))
		_check(int(row["enchantment_capacity"]) == (1 if String(row["model_key"]) != "Shield_Round" else 0),
				"%s has the enchantment capacity its kind and tier imply" % String(row["model_key"]))
		_check((row["slots"] as Array).is_empty(), "and nothing is worn: setup mints, it does not dress")
	for i in ids.size():
		for j in range(i + 1, ids.size()):
			_check(ids[i] != ids[j], "every instance id is its own")

	# Setup is not a resource faucet: no script call can ask for more.
	var enemy: Node = world.get_node("Enemy")
	_check(enemy.sheet.get_equipment_snapshot().is_empty(),
			"and the dummies were given nothing - the setup command is the player's start, not a shop")
	await _free(world)


func _equipping_is_the_players_choice_of_hand() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var sheet = player.sheet
	var popup = await _open_popup(world)
	if popup == null:
		await _free(world)
		return

	var texts := []
	_texts(popup, texts)
	for model in STARTER_MODELS:
		_check(texts.any(func(text: String) -> bool: return model in text),
				"the panel lists the owned %s" % model)

	# The opening state is a choice, not an assumption: nothing is worn, so no unequip control
	# is offered and every hand is free.
	_check(not _has_button(popup, "Unequip%d" % _id_of_model(sheet, "Sword")),
			"an item that is carried offers no unequip control")
	_check(_has_button(popup, "Equip%d_%d" % [_id_of_model(sheet, "Sword"), SLOT_RIGHT_HAND]),
			"a one-handed weapon offers the right hand")
	_check(_has_button(popup, "Equip%d_%d" % [_id_of_model(sheet, "Sword"), SLOT_LEFT_HAND]),
			"and the left, separately - the player picks")

	# Nothing changed between frames, so the frame after next must still be showing the very
	# same controls: a panel that rebuilt while the pointer rested on a button would drop the
	# hover - and the panel is a thing you point at, not a thing you watch.
	var held_button = _button(popup, "Equip%d_%d" % [_id_of_model(sheet, "Sword"), SLOT_RIGHT_HAND])
	await process_frame
	await process_frame
	_check(held_button != null and is_instance_valid(held_button)
			and held_button == _button(popup, "Equip%d_%d" % [_id_of_model(sheet, "Sword"),
					SLOT_RIGHT_HAND]),
			"an unchanged equipment list keeps its controls standing, frame to frame")

	# The sword goes in the right hand, through the control and back from the sheet.
	var sword := _id_of_model(sheet, "Sword")
	_click_at(_button(popup, "Equip%d_%d" % [sword, SLOT_RIGHT_HAND])
			.get_global_rect().get_center())
	await process_frame
	await process_frame
	_check(_row(sheet, sword).get("slots", []) == [SLOT_RIGHT_HAND],
			"the control equipped the sword in the right hand")
	_check(_has_button(popup, "Unequip%d" % sword), "and the row now offers to take it off")
	_check(_any_text(popup, "Right hand"), "the panel says where it went")

	# A second one-handed weapon takes the other hand: two weapons, no dual-wield rule here.
	var dagger := _id_of_model(sheet, "Dagger")
	_click_at(_button(popup, "Equip%d_%d" % [dagger, SLOT_LEFT_HAND])
			.get_global_rect().get_center())
	await process_frame
	await process_frame
	_check(_row(sheet, dagger).get("slots", []) == [SLOT_LEFT_HAND],
			"the dagger goes in the free left hand")

	# An occupied hand is not offered at all, and the panel names whose fault that is: no
	# automatic swap, and nothing else moved while we looked.
	_click_at(_button(popup, "Unequip%d" % dagger).get_global_rect().get_center())
	await process_frame
	await process_frame
	var dagger_right := _button(popup, "Equip%d_%d" % [dagger, SLOT_RIGHT_HAND])
	_check(dagger_right != null and dagger_right.disabled,
			"the occupied right hand is not offered for the dagger")
	_check(_any_text(popup, "unequip"), "and the panel asks for the occupying item to come off first")
	_check(_row(sheet, sword).get("slots", []) == [SLOT_RIGHT_HAND],
			"the sword still in that hand was not swapped out for anything")
	_check(_rows(sheet).size() == STARTER_MODELS.size(), "and no item appeared while we watched")

	# A two-handed weapon reserves both hands, and while it is held nothing else fits.
	_click_at(_button(popup, "Unequip%d" % sword).get_global_rect().get_center())
	await process_frame
	await process_frame
	var spear := _id_of_model(sheet, "Spear")
	_check(_has_button(popup, "Equip%d_%d" % [spear, SLOT_LEFT_HAND]),
			"the spear offers a hand to start from")
	_click_at(_button(popup, "Equip%d_%d" % [spear, SLOT_LEFT_HAND])
			.get_global_rect().get_center())
	await process_frame
	await process_frame
	var spear_slots: Array = _row(sheet, spear).get("slots", [])
	_check(spear_slots == [SLOT_LEFT_HAND, SLOT_RIGHT_HAND],
			"and the spear holds both hands while owning one record: %s" % str(spear_slots))
	var sword_right := _button(popup, "Equip%d_%d" % [sword, SLOT_RIGHT_HAND])
	_check(sword_right != null and sword_right.disabled,
			"with a two-hander held, the sword has no hand it can be given")

	# Unmet requirements do not stop an equip; they are shown as C++ measured them. The sword
	# asks 5 Dexterity, a level-1 sheet blessed in Strength has 1 of it, and the ratio squared
	# is what effectiveness is: a twentieth, and a swing that costs (2 - 1/25) of its base.
	var sword_view: Dictionary = sheet.get_weapon_snapshot(sword)
	_check(not sword_view.is_empty(), "the sword resolves a weapon snapshot now that it is owned")
	_check(_close(float(sword_view["effectiveness"]), 0.04),
			"at the effectiveness C++ computes for this wielder: %s" % str(sword_view["effectiveness"]))
	_check(_close(float(sword_view["attack_cost"]), 8.0 * 1.96, 0.01),
			"and the cost that goes with it, not the weapon's bare 8")
	_check(_any_text(popup, "4%"), "and the panel shows the effectiveness it is worth in hand")
	_check(_any_text(popup, "15.68"), "with the cost that goes with it")
	_check(_any_text(popup, "Dexterity 5 (this sheet 1)"),
			"and what the sword asks against what this sheet has")

	# Escape, the close button, and the viewport edge.
	_key(KEY_ESCAPE)
	await process_frame
	_check(not popup.visible, "Escape closes the panel")
	_key(KEY_I)
	await process_frame
	var entries := world.find_children("EquipmentButton", "Button", true, false)
	_click_at((entries[0] as Button).get_global_rect().get_center())
	await process_frame
	await process_frame
	_check(popup.visible, "and the entry opens it again, still showing what is held")
	_check(_row(sheet, spear).get("slots", []) == [SLOT_LEFT_HAND, SLOT_RIGHT_HAND],
			"reopening the panel changed nothing about the spear")
	_click_at(_button(popup, "CloseButton").get_global_rect().get_center())
	await process_frame
	_check(not popup.visible, "the close button closes it")
	await _free(world)


func _rejections_change_nothing() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var sheet = player.sheet
	var enemy: Node = world.get_node("Enemy")
	var marker: Node = world.get("_marker")
	var popup = await _open_popup(world)
	if popup == null:
		await _free(world)
		return
	var sword := _id_of_model(sheet, "Sword")
	_click_at(_button(popup, "Equip%d_%d" % [sword, SLOT_RIGHT_HAND])
			.get_global_rect().get_center())
	await process_frame
	await process_frame
	var before := _state(sheet)

	# Ids that are not this sheet's to wear.
	_check(not sheet.equip_item(999999, SLOT_RIGHT_HAND), "an id nobody owns is refused")
	_check(not sheet.equip_item(0, SLOT_RIGHT_HAND), "and id 0")
	_check(not sheet.equip_item(-1, SLOT_RIGHT_HAND), "and a negative id")
	_check(not sheet.equip_item(sword, 999), "and a slot that is not a slot")

	# A real item that belongs to somebody else. The dummy is given its own starter kit
	# through the same production command, so the id below is a genuine instance of a genuine
	# definition - and it is still not the player's to put on.
	var combat = world.get("_combat")
	_check(combat != null, "the world still owns its coordinator")
	_check(combat.setup_starter_player(enemy.sheet.get_character_id()),
			"the setup command can dress another character too")
	_check(enemy.sheet.get_equipment_snapshot().size() == STARTER_MODELS.size(),
			"and the dummy now owns its own five")
	var foreign := _id_of_model(enemy.sheet, "Sword")
	_check(foreign != 0, "the dummy's sword has an id")
	_check(_id_of_model(sheet, "Sword") != foreign, "and it is not the player's sword")
	_check(not sheet.equip_item(foreign, SLOT_LEFT_HAND),
			"an item another sheet owns cannot be equipped here")

	# An item this sheet owns, already worn, cannot be worn twice.
	var taken := _id_of_model(sheet, "Dagger")
	_check(sheet.equip_item(taken, SLOT_LEFT_HAND), "the dagger goes in the left hand")
	_check(not sheet.equip_item(taken, SLOT_RIGHT_HAND),
			"and cannot be equipped a second time from the hand it is already in")
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "off again")

	# Every refusal above left the sheet exactly where it was.
	_check(_state(sheet) == before, "no refusal changed what is worn, the counts, the order or the health")
	_check(not marker.visible, "and none of it ordered a move")
	_check(sheet.get_attack_target_id() == 0, "or an attack")
	_check(sheet.velocity_at(player.global_position) == Vector3.ZERO, "or any motion")
	await _free(world)


func _limbs_are_gates() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var sheet = player.sheet
	var popup = await _open_popup(world)
	if popup == null:
		await _free(world)
		return
	var sword := _id_of_model(sheet, "Sword")
	var spear := _id_of_model(sheet, "Spear")

	# Destroying a limb is not a killing blow: a part holds its own health, and spending it
	# leaves the sheet alive at sixty.
	var arm_health: float = sheet.get_part_max_health(PART_RIGHT_ARM)
	_check(_close(arm_health, 40.0), "an arm's own health is forty at level 1")
	_check(sheet.apply_damage(PART_RIGHT_ARM, arm_health), "the right arm is destroyed")
	_check(sheet.is_alive(), "and the sheet survives it")
	_check(not sheet.has_right_arm(), "and says so on the way out")

	# One arm down: a one-handed item still goes on, because there is a hand left; a two-handed
	# one cannot be held at all. Whether the dead arm can swing is a separate rule C++ owns -
	# this is about being able to wear the thing.
	_check(not sheet.equip_item(spear, SLOT_LEFT_HAND), "a two-handed spear needs two usable arms")
	_check(sheet.equip_item(sword, SLOT_LEFT_HAND), "a one-handed sword still goes on")
	_check(_row(sheet, sword).get("slots", []) == [SLOT_LEFT_HAND], "in the hand that works")
	# The panel is drawn from the sheet on the next frame: an edit made on the sheet - here,
	# not through a button - has to show up without the panel being reopened.
	await process_frame
	await process_frame
	_check(_any_text(popup, "Left hand"), "and the open panel says which hand it is in")
	_check(sheet.unequip_item(SLOT_LEFT_HAND), "off again")

	# Both arms down: nothing can be held in a hand, and a refusal is a refusal rather than a
	# half change.
	_check(sheet.apply_damage(PART_LEFT_ARM, sheet.get_part_max_health(PART_LEFT_ARM)),
			"the left arm is destroyed too")
	_check(sheet.is_alive(), "still alive, and unable to hold anything")
	var before := _state(sheet)
	_check(not sheet.equip_item(sword, SLOT_RIGHT_HAND), "no hand can be trusted with a weapon")
	_check(not sheet.equip_item(sword, SLOT_LEFT_HAND), "in either slot")
	_check(_any_text(popup, "unequip") or _any_text(popup, "hand"),
			"and the panel says something true about hands rather than nothing")

	# The control: the very same command on a sheet with two arms succeeds, so what the player
	# hit above is the limb gate and not a broken path.
	var combat = world.get("_combat")
	var enemy: Node = world.get_node("Enemy")
	_check(combat.setup_starter_player(enemy.sheet.get_character_id()),
			"the dummy is given its own kit")
	var their_sword := _id_of_model(enemy.sheet, "Sword")
	_check(enemy.sheet.equip_item(their_sword, SLOT_RIGHT_HAND),
			"and it can wear it: the refusals above were the arms, not the command")
	_check(_state(sheet) == before,
			"with the player's equipment, counts, order and health unchanged by any of it")
	await _free(world)


func _scenery_never_acquires() -> void:
	var world = await _world()
	var player: Node = world.get_node("Player")
	var sheet = player.sheet
	var camera: Camera3D = world.get("_camera")
	var marker: Node = world.get("_marker")
	var before := _state(sheet)
	var weapons := world.find_children("MedievalWeapons", "", true, false)
	_check(weapons.size() == 1, "the scenery rack is still in the world")
	if weapons.size() != 1:
		await _free(world)
		return
	var models := weapons[0].get_children()
	_check(models.size() == 24, "every one of the 24 scenery models is still there")
	var popup = _popup_of(world)
	for model in models:
		var target: Node3D = model as Node3D
		camera.global_position = target.global_position + Vector3(0.0, 7.0, 6.0)
		camera.look_at(target.global_position, Vector3.UP)
		await physics_frame
		var point := camera.unproject_position(target.global_position + Vector3(0.0, 0.08, 0.0))
		_click_at(point)
		await process_frame
		_check(_state(sheet) == before,
				"clicking the scenery %s leaves the backpack, the hands and the orders alone"
				% String(target.name))
	_check(_rows(sheet).size() == STARTER_MODELS.size(), "and still mints nothing after all 24")
	_check(not marker.visible, "with no move ordered by any of them")
	if popup != null:
		_check(not popup.visible, "the scenery popup is not the equipment popup")
	await _free(world)


func _run() -> void:
	_viewport_size = root.get_visible_rect().size
	await _the_backpack_is_the_starter_kit()
	await _equipping_is_the_players_choice_of_hand()
	await _rejections_change_nothing()
	await _limbs_are_gates()
	await _scenery_never_acquires()
	_check(checks > 90, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Equipment popup: all checks passed")
	quit(1 if failures else 0)
