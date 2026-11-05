extends SceneTree

# Task 2 popup tests. The I-key character screen must: start hidden, toggle on
# I (never on key echo), close on ×, show the ten attribute names and the 35
# basic skill names with their effective values, show a chosen specialization
# beneath its basic skill and no other advanced option, show no HP/stamina/mana
# pool rows, stay live while open, and consume its own clicks so the world keeps
# working outside it.

var failures := 0

const ATTRIBUTES := ["Strength", "Dexterity", "Agility", "Toughness", "Constitution", "Perception",
	"Intelligence", "Charisma", "Luck", "Profanity"]

# The rule-table names the bridge must supply; the popup must not invent them.
const BASICS := ["One-Handed Weapons", "Two-Handed Weapons", "Polearms", "Unarmed Combat", "Archery",
	"Blocking", "Dodging", "Athletics", "Acrobatics", "Swimming", "Endurance", "Hunting", "Foraging",
	"Medicine", "Animal Handling", "Smithing", "Construction", "Cooking", "Alchemy", "Research",
	"Mining", "Farming", "Persuasion", "Intimidation", "Trading", "Leadership", "Spellcasting",
	"Casting Speed", "Mana Control", "Elemental Magic", "Restoration", "Enchanting", "Lockpicking",
	"Thievery", "Stealth"]

const UNCHOSEN_ADVANCED := ["Katana", "Sword", "Greatsword", "Spear", "Boxing", "Bow", "Big Shields",
	"Footwork", "Sprinting", "Climbing", "Trapping", "Herbalism", "Surgery", "Mounts", "Weaponsmith",
	"Fortification", "Gourmet Cooking", "Potions", "Arcane Research", "Diplomacy", "Channeling",
	"Fire", "Healing", "Pickpocket", "Infiltration"]

const POOL_NAMES := ["HP", "Stamina", "Mana"]


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _texts(node: Node) -> PackedStringArray:
	var found := PackedStringArray()
	for label in node.find_children("*", "Label", true, false):
		found.append(String(label.text))
	return found


func _label_with_text(node: Node, text: String) -> Label:
	for label in node.find_children("*", "Label", true, false):
		if String(label.text) == text:
			return label as Label
	return null


# Rows are two-column grids laid out as name, value, name, value...
func _row_value(label: Label) -> float:
	var kids: Array = label.get_parent().get_children()
	var index := kids.find(label)
	if index < 0 or index + 1 >= kids.size():
		return NAN
	return float(String(kids[index + 1].text))


func _press_i(echo: bool = false) -> void:
	var key := InputEventKey.new()
	key.keycode = KEY_I
	key.physical_keycode = KEY_I
	key.pressed = true
	key.echo = echo
	root.push_input(key)


# Route viewport-local clicks through Godot's GUI and unhandled-input pipeline.
func _click_at(viewport_pos: Vector2) -> void:
	var motion := InputEventMouseMotion.new()
	motion.position = viewport_pos
	motion.global_position = viewport_pos
	root.push_input(motion, true)
	var down := InputEventMouseButton.new()
	down.button_index = MOUSE_BUTTON_LEFT
	down.position = viewport_pos
	down.global_position = viewport_pos
	down.pressed = true
	root.push_input(down, true)
	var up := InputEventMouseButton.new()
	up.button_index = MOUSE_BUTTON_LEFT
	up.position = viewport_pos
	up.global_position = viewport_pos
	up.pressed = false
	root.push_input(up, true)


func _floor_click_outside(popup: Control, camera: Camera3D, player: Node) -> Vector2:
	for offset in [Vector3(0.0, 0.0, 7.0), Vector3(-7.0, 0.0, 3.0), Vector3(7.0, 0.0, 3.0)]:
		var screen: Vector2 = camera.unproject_position(player.global_position + offset)
		if not popup.get_global_rect().has_point(screen):
			return screen
	return Vector2.INF


func _run() -> void:
	var world: Node = load("res://scenes/main.tscn").instantiate()
	root.add_child(world)
	await process_frame
	await physics_frame

	var popups: Array = world.find_children("CharacterPopup", "", true, false)
	_check(popups.size() == 1, "the world carries exactly one character popup")
	if popups.size() != 1:
		quit(1)
		return
	var popup: Control = popups[0]
	var player: Node = world.get_node("Player")
	var marker: Node = world.get("_marker")
	var camera: Camera3D = world.get("_camera")

	_check(not popup.visible, "the popup starts hidden")
	_check(popup.mouse_filter == Control.MOUSE_FILTER_STOP, "the popup stops mouse input inside it")

	# I toggles, echo never does.
	_press_i()
	_check(popup.visible, "I opens the popup")
	_press_i(true)
	_press_i(true)
	_check(popup.visible, "repeated key echo does not toggle the popup")
	_press_i()
	_check(not popup.visible, "I closes the popup")
	_press_i()
	await process_frame
	_check(popup.visible, "the popup is open for the content checks")
	await process_frame
	_check(_close(popup.modulate.a, 0.9), "the popup is 90 percent opaque")
	# The approved design uses a dark, framed panel with warm readable text.
	_check(popup.has_theme_stylebox_override("panel"), "the sheet has its own medieval frame")
	var panel_style := popup.get_theme_stylebox("panel")
	_check(panel_style is StyleBoxFlat, "the sheet frame has a solid background")
	if panel_style is StyleBoxFlat:
		_check(panel_style.bg_color.r > panel_style.bg_color.b and panel_style.bg_color.r < 0.3,
			"the sheet background is dark and warm")
		_check(panel_style.border_width_left > 0 and panel_style.border_color.r > panel_style.border_color.b,
			"the sheet has a warm metal-colored border")
	var attribute_label := _label_with_text(popup, "Strength")
	if attribute_label != null:
		var ink := attribute_label.get_theme_color("font_color")
		_check(ink.r > 0.7 and ink.g > 0.6 and ink.b < ink.r,
			"attribute text uses readable parchment tones")

	# Drag the title bar through the real GUI input pipeline. The panel should
	# move with the pointer and stay put on later frames and after reopening.
	var header: Control = popup.find_child("Header", true, false)
	_check(header != null, "the popup has a draggable header")
	if header != null:
		var start := header.get_global_rect().position + Vector2(50.0, 12.0)
		var distance := Vector2(70.0, 35.0)
		var before := popup.position
		var down := InputEventMouseButton.new()
		down.button_index = MOUSE_BUTTON_LEFT
		down.position = start
		down.global_position = start
		down.pressed = true
		root.push_input(down, true)
		var motion := InputEventMouseMotion.new()
		motion.position = start + distance
		motion.global_position = motion.position
		motion.relative = distance
		motion.button_mask = MOUSE_BUTTON_MASK_LEFT
		root.push_input(motion, true)
		var up := InputEventMouseButton.new()
		up.button_index = MOUSE_BUTTON_LEFT
		up.position = start + distance
		up.global_position = up.position
		up.pressed = false
		root.push_input(up, true)
		await process_frame
		await process_frame
		_check(popup.position.distance_to(before + distance) < 0.1, "dragging the header moves the popup")
		_press_i()
		_press_i()
		await process_frame
		_check(popup.position.distance_to(before + distance) < 0.1, "the dragged position survives reopening")

	# All names, only effective values, no pools, no unchosen advanced options.
	var texts := _texts(popup)
	for attribute in ATTRIBUTES:
		_check(attribute in texts, "the popup shows the attribute %s" % attribute)
	for skill in BASICS:
		_check(skill in texts, "the popup shows the basic skill %s" % skill)
	for advanced in UNCHOSEN_ADVANCED:
		_check(advanced not in texts, "an unchosen advanced option (%s) is absent" % advanced)
	for pool in POOL_NAMES:
		_check(pool not in texts, "the popup shows no %s pool row" % pool)
	var strength_label := _label_with_text(popup, "Strength")
	_check(strength_label != null, "the Strength row exists")
	if strength_label != null:
		_check(_close(_row_value(strength_label), 1.25), "Strength shows the effective 1.25")
	var one_handed_label := _label_with_text(popup, "One-Handed Weapons")
	_check(one_handed_label != null, "the One-Handed Weapons row exists")
	if one_handed_label != null:
		_check(_close(_row_value(one_handed_label), 1.125), "One-Handed Weapons shows the effective 1.125")
	var scrollers: Array = popup.find_children("SkillsScroll", "ScrollContainer", true, false)
	_check(scrollers.size() == 1, "the grouped skills have a scroll container")
	if scrollers.size() == 1:
		var scroller: ScrollContainer = scrollers[0]
		var wheel := InputEventMouseButton.new()
		wheel.button_index = MOUSE_BUTTON_WHEEL_DOWN
		wheel.position = scroller.get_global_rect().get_center()
		wheel.global_position = wheel.position
		wheel.pressed = true
		root.push_input(wheel, true)
		wheel.pressed = false
		root.push_input(wheel, true)
		await process_frame
		_check(scroller.scroll_vertical > 0, "the mouse wheel scrolls the skills")

	# A real GUI click on the × button closes the popup.
	var closers: Array = popup.find_children("CloseButton", "", true, false)
	_check(closers.size() == 1, "the popup has a close button")
	if closers.size() == 1:
		_click_at((closers[0] as Control).get_global_rect().get_center())
		_check(not popup.visible, "clicking the close button closes the popup")
	_press_i()
	await process_frame

	# A controlled chosen-specialization snapshot renders its name and its own
	# rank-one value; the live loop then refreshes back to the bound sheet while open.
	popup.render_snapshot([{
		"name": "Smithing",
		"value": 2.5,
		"advanced_name": "Weaponsmith",
		"advanced_value": 1.125,
	}])
	var smithing_label := _label_with_text(popup, "Smithing")
	var weaponsmith_label := _label_with_text(popup, "Weaponsmith")
	_check(smithing_label != null and weaponsmith_label != null, "the chosen specialization renders")
	if smithing_label != null and weaponsmith_label != null:
		_check(_close(_row_value(smithing_label), 2.5), "the injected basic row shows its value")
		_check(_close(_row_value(weaponsmith_label), 1.125),
			"the chosen specialization shows its rank-one value")
	await process_frame
	texts = _texts(popup)
	_check("One-Handed Weapons" in texts, "the popup refreshes from its sheet while open")
	_check("Weaponsmith" not in texts, "and the injected snapshot is gone after the refresh")

	# A different data source while open changes the displayed values.
	var veteran := OpenCharacter.new()
	_check(veteran.configure_npc(99, "Veteran", 0, 7, 4242), "the veteran sheet configures")
	popup.bind_sheet(veteran)
	strength_label = _label_with_text(popup, "Strength")
	if strength_label != null:
		var want := 125.0 if veteran.get_blessing() == 0 else 100.0
		_check(_close(_row_value(strength_label), want), "the displayed strength follows the bound sheet")
	popup.bind_sheet(player.sheet)
	strength_label = _label_with_text(popup, "Strength")
	if strength_label != null:
		_check(_close(_row_value(strength_label), 1.25), "and follows it back")

	# The naming rule is one rule for the whole HUD (docs/status_UID.md, locked 2026-11-01):
	# the sheet's name, the actor's scene-node name under it, and 19 characters plus an ellipsis
	# past twenty. A popup keeping its own idea of a name would be the second rule the lock
	# forbids - and it was, until the review looked.
	var long_named := OpenCharacter.new()
	_check(long_named.configure_npc(98, "Bartholomew the Unready", 0, 1, 77),
			"a sheet with a long name configures")
	popup.bind_sheet(long_named, "ActorNode")
	var title: Label = popup.get("_title_label")
	_check(title != null and String(title.text) == "Bartholomew the Unr…",
			"the popup title truncates like every other view: got [%s]"
					% ("" if title == null else String(title.text)))
	var nameless := OpenCharacter.new()
	_check(nameless.configure_npc(97, "", 0, 1, 78), "a sheet with no name configures")
	popup.bind_sheet(nameless, "NamelessNode")
	_check(title != null and String(title.text) == "NamelessNode",
			"an unnamed sheet falls back to the actor's node name: got [%s]"
					% ("" if title == null else String(title.text)))
	var unbindable := OpenCharacter.new()
	_check(unbindable.configure_npc(96, "A Name Long Enough To Need Cutting", 0, 1, 79),
			"a third sheet configures")
	popup.bind_sheet(unbindable)
	_check(title != null and String(title.text) == "A Name Long Enough …",
			"and an unnamed actor fallback still truncates: got [%s]"
					% ("" if title == null else String(title.text)))
	popup.bind_sheet(player.sheet, String(player.name))

	# An unconfigured sheet invents nothing.
	var bare := CharacterPopup.new()
	root.add_child(bare)
	bare.bind_sheet(OpenCharacter.new())
	var bare_texts := _texts(bare)
	_check(not "Strength" in bare_texts, "an unconfigured sheet shows no attribute names")
	_check(not "One-Handed Weapons" in bare_texts, "an unconfigured sheet shows no skill names")
	bare.free()

	# Click isolation: inside is consumed, outside still orders, walking continues.
	player.sheet.clear_order()
	marker.visible = false
	await physics_frame
	var popup_center: Vector2 = popup.get_global_rect().get_center()
	_click_at(popup_center)
	_check(marker != null and not marker.visible, "a click inside the popup shows no move marker")
	_check(player.sheet.velocity_at(player.global_position) == Vector3.ZERO,
		"and issues no move order")
	var outside := _floor_click_outside(popup, camera, player)
	_check(outside != Vector2.INF, "a floor point outside the popup is reachable on screen")
	if outside != Vector2.INF:
		_click_at(outside)
		_check(marker.visible, "a click outside the popup still marks the floor")
		_check(player.sheet.velocity_at(player.global_position).length() > 0.0,
			"and still becomes a move order while the popup is open")
	var walked_from: Vector3 = player.global_position
	for frame in range(30):
		await physics_frame
	_check(player.global_position.distance_to(walked_from) > 0.5,
		"an existing move order keeps walking while the popup is open")

	# Closed: normal floor clicks work as before.
	_press_i()
	_check(not popup.visible, "I closes the popup again")
	player.sheet.clear_order()
	marker.visible = false
	await physics_frame
	outside = _floor_click_outside(popup, camera, player)
	if outside != Vector2.INF:
		_click_at(outside)
		_check(marker.visible, "with the popup closed a floor click still marks")
		_check(player.sheet.velocity_at(player.global_position).length() > 0.0,
			"and still walks the player")

	world.queue_free()
	await process_frame
	if failures == 0:
		print("Character popup: all checks passed")
	quit(1 if failures else 0)
