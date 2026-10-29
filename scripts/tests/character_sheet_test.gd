extends SceneTree

# Task 7 bridge test. It exercises OpenCharacter directly, with no scene and no
# model, so a bridge that loses a method, throws across the Godot boundary, or
# starts answering from an unconfigured sheet fails here.

var failures := 0


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _configured_sheet() -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(1, "Walker", 0, 1, 0), "a valid player sheet configures")
	return sheet


func _run() -> void:
	var bare := OpenCharacter.new()
	_check(not bare.is_configured(), "a fresh bridge is unconfigured")
	_check(bare.get_faction() == -1, "an unconfigured faction reports -1")
	_check(bare.get_character_id() == 0, "an unconfigured id is zero")
	_check(bare.get_character_name() == "", "an unconfigured name is empty")
	_check(bare.get_level() == 0, "an unconfigured level is zero")
	_check(bare.get_blessing() == -1, "an unconfigured blessing reports -1")
	_check(bare.get_health() == 0.0 and bare.get_max_health() == 0.0, "an unconfigured sheet has no hp")
	_check(bare.get_stamina() == 0.0 and bare.get_max_stamina() == 0.0, "an unconfigured sheet has no stamina")
	_check(bare.get_mana() == 0.0 and bare.get_max_mana() == 0.0, "an unconfigured sheet has no mana")
	_check(bare.get_strength() == 0.0, "an unconfigured sheet has no strength")
	_check(bare.get_attribute(0) == 0.0 and bare.get_attribute_base(0) == 0.0, "an unconfigured attribute is zero")
	_check(bare.get_part_health(0) == 0.0 and bare.get_part_max_health(0) == 0.0, "an unconfigured part is zero")
	_check(not bare.is_alive() and not bare.can_walk(), "an unconfigured sheet is neither alive nor walking")
	_check(bare.get_walk_speed() == 0.0, "an unconfigured walk speed is zero")
	_check(not bare.has_left_arm() and not bare.has_right_leg(), "an unconfigured sheet has no limbs")
	_check(not bare.get_can_move(), "an unconfigured sheet cannot move")
	_check(bare.velocity_at(Vector3.ZERO) == Vector3.ZERO, "an unconfigured sheet does not move")
	_check(not bare.apply_damage(0, 10.0), "damage on an unconfigured sheet fails")
	_check(not bare.regenerate(1.0, false), "regen on an unconfigured sheet fails")
	_check(not bare.set_destination(Vector3(1.0, 0.0, 0.0)), "an order on an unconfigured sheet fails")
	bare.clear_order()
	bare.set_can_move(true)
	_check(not bare.is_configured(), "a mutation on an unconfigured sheet configures nothing")
	_check(bare.velocity_at(Vector3.ZERO) == Vector3.ZERO, "and it still does not move")

	var bad_id := OpenCharacter.new()
	_check(not bad_id.configure(0, "Nobody", 0, 1, 0), "a zero character id is refused")
	_check(not bad_id.is_configured(), "a failed configure leaves the sheet unconfigured")
	var bad_faction := OpenCharacter.new()
	_check(not bad_faction.configure(1, "Odd", 5, 1, 0), "an out-of-range faction is refused")
	_check(not bad_faction.configure(1, "Odd", -1, 1, 0), "a negative faction is refused")
	_check(not bad_faction.is_configured(), "a failed faction leaves the sheet unconfigured")
	var bad_level := OpenCharacter.new()
	_check(not bad_level.configure(1, "Odd", 0, 8, 0), "level eight is refused")
	_check(not bad_level.configure(1, "Odd", 0, 0, 0), "level zero is refused")
	_check(not bad_level.is_configured(), "a failed level leaves the sheet unconfigured")
	var bad_blessing := OpenCharacter.new()
	_check(not bad_blessing.configure(1, "Odd", 0, 1, 10), "blessing ten is refused")
	_check(not bad_blessing.configure(1, "Odd", 0, 1, -1), "blessing minus one is refused")
	_check(not bad_blessing.is_configured(), "a failed blessing leaves the sheet unconfigured")

	var sheet := _configured_sheet()
	_check(sheet.is_configured(), "the sheet is configured")
	_check(sheet.get_character_id() == 1, "the id is readable")
	_check(sheet.get_character_name() == "Walker", "the name is readable")
	_check(sheet.get_faction() == 0, "friendly is still zero on the bridge")
	_check(sheet.get_level() == 1, "the level is readable")
	_check(sheet.get_blessing() == 0, "the explicit blessing is readable")
	_check(_close(sheet.get_attribute_base(0), 1.0), "the strength base is one")
	_check(_close(sheet.get_attribute(0), 1.25), "the blessed strength is 1.25 at level one")
	_check(_close(sheet.get_attribute(1), 1.0), "an unblessed attribute is one")
	_check(_close(sheet.get_strength(), 1.25), "get_strength now reports the effective attribute")
	_check(_close(sheet.get_health(), 100.0) and _close(sheet.get_max_health(), 100.0), "hp starts full")
	_check(_close(sheet.get_stamina(), 100.0) and _close(sheet.get_max_stamina(), 100.0), "stamina starts full")
	_check(_close(sheet.get_mana(), 100.0) and _close(sheet.get_max_mana(), 100.0), "mana starts full")
	_check(_close(sheet.get_part_health(0), 25.0), "the head starts at twenty-five")
	_check(_close(sheet.get_part_max_health(1), 50.0), "the torso maximum is fifty")
	_check(_close(sheet.get_part_max_health(2), 40.0), "a limb maximum is forty")
	_check(sheet.is_alive() and sheet.can_walk(), "the configured sheet walks")
	_check(_close(sheet.get_walk_speed(), 4.5), "the walk speed comes from C++")
	_check(sheet.has_left_arm() and sheet.has_right_arm() and sheet.has_left_leg() and sheet.has_right_leg(),
		"all four limbs report healthy")
	_check(sheet.get_can_move(), "the movement switch starts on")
	_check(not sheet.configure(2, "Second", 0, 1, 0), "a second successful configuration is refused")
	_check(sheet.get_character_id() == 1, "the refused configure changed nothing")

	_check(sheet.apply_damage(0, 10.0), "a head hit is accepted")
	_check(_close(sheet.get_part_health(0), 15.0), "the head lost ten")
	_check(_close(sheet.get_health(), 90.0), "main hp lost ten too")
	_check(sheet.regenerate(5.0, false), "regen is accepted while alive")
	_check(_close(sheet.get_health(), 90.0), "regen never heals hp")

	sheet.set_destination(Vector3(5.0, 0.0, 0.0))
	var walked: Vector3 = sheet.velocity_at(Vector3.ZERO)
	_check(_close(walked.x, 4.5) and _close(walked.y, 0.0) and _close(walked.z, 0.0),
		"velocity needs no speed from Godot")
	_check(_close(sheet.velocity_at(Vector3(4.8, 0.0, 0.0)).length(), 0.0), "arriving close enough stops")
	sheet.set_can_move(false)
	_check(sheet.velocity_at(Vector3.ZERO) == Vector3.ZERO, "the raw switch stops a healthy walker")
	sheet.set_can_move(true)
	_check(_close(sheet.velocity_at(Vector3.ZERO).x, 4.5), "switching back on resumes")
	sheet.clear_order()
	_check(sheet.velocity_at(Vector3.ZERO) == Vector3.ZERO, "clear_order stops")

	var legged := _configured_sheet()
	legged.set_destination(Vector3(5.0, 0.0, 0.0))
	_check(_close(legged.velocity_at(Vector3.ZERO).x, 4.5), "a healthy walk starts at 4.5")
	_check(legged.apply_damage(4, 40.0), "a leg hit is accepted")
	_check(_close(legged.get_walk_speed(), 2.25), "one dead leg halves the walk speed")
	_check(_close(legged.velocity_at(Vector3.ZERO).x, 2.25), "and the velocity")
	_check(legged.can_walk(), "one dead leg still walks")
	_check(legged.apply_damage(5, 40.0), "the other leg hit is accepted")
	_check(_close(legged.get_walk_speed(), 0.0), "both legs stop walking")
	_check(not legged.can_walk(), "can_walk is false")
	_check(legged.velocity_at(Vector3.ZERO) == Vector3.ZERO, "and the sheet does not move")
	_check(legged.get_can_move(), "the raw switch is untouched by the legs")
	_check(_close(legged.get_health(), 20.0), "two leg hits cost forty hp each")
	_check(legged.is_alive(), "legs do not kill")

	var dead := _configured_sheet()
	dead.set_destination(Vector3(5.0, 0.0, 0.0))
	_check(dead.apply_damage(1, 50.0), "a torso hit is accepted")
	_check(not dead.is_alive(), "a zero torso kills")
	_check(_close(dead.get_health(), 50.0), "main hp paid once")
	_check(not dead.can_walk(), "a dead sheet cannot walk")
	_check(dead.velocity_at(Vector3.ZERO) == Vector3.ZERO, "a dead sheet does not move")
	_check(not dead.regenerate(10.0, false), "a dead sheet cannot regenerate")
	_check(not dead.set_destination(Vector3(1.0, 0.0, 0.0)), "a dead sheet takes no order")
	_check(dead.get_part_health(1) == 0.0, "the killed part reads zero")
	_check(dead.has_left_arm(), "a limb boolean means that part is still above zero")
	_check(not dead.can_walk(), "even with arms up, a dead sheet cannot walk")

	var named := OpenCharacter.new()
	_check(named.configure(7, "", 1, 7, 9), "an empty name and the extremes are accepted")
	_check(named.is_configured(), "the sheet is configured")
	_check(named.get_character_name() == "", "the empty name is kept")
	_check(named.get_faction() == 1, "enemy is still one on the bridge")
	_check(named.get_level() == 7, "level seven is accepted")
	_check(named.get_blessing() == 9, "blessing nine is accepted")
	_check(_close(named.get_attribute(9), 125.0), "a blessed attribute at level seven is 125")
	_check(_close(named.get_attribute(0), 100.0), "an unblessed attribute at level seven is 100")
	_check(_close(named.get_max_health(), 100.0), "level seven does not stretch the hp bar")

	var first_npc := OpenCharacter.new()
	var second_npc := OpenCharacter.new()
	var third_npc := OpenCharacter.new()
	_check(first_npc.configure_npc(2, "", 1, 1, 2002), "an npc sheet configures from a seed")
	_check(second_npc.configure_npc(3, "Nameless", 1, 1, 2002), "the same seed configures again")
	_check(third_npc.configure_npc(4, "", 0, 4, 999999), "another seed configures")
	_check(first_npc.is_configured() and second_npc.is_configured(), "both npc sheets are configured")
	_check(first_npc.get_blessing() >= 0 and first_npc.get_blessing() <= 9, "the npc blessing is one of ten")
	_check(first_npc.get_blessing() == second_npc.get_blessing(), "one seed gives one blessing")
	_check(first_npc.get_character_name() == "" and second_npc.get_character_name() == "Nameless",
		"npc names are kept")
	_check(not OpenCharacter.new().configure_npc(0, "", 0, 1, 2002), "an npc with a zero id is refused")
	_check(not OpenCharacter.new().configure_npc(2, "", 3, 1, 2002), "an npc with a bad faction is refused")
	_check(not OpenCharacter.new().configure_npc(2, "", 0, 9, 2002), "an npc with a bad level is refused")

	_check(_close(sheet.get_attribute(99), 0.0), "an out-of-range attribute id reports zero")
	_check(_close(sheet.get_attribute_base(-1), 0.0), "a negative attribute id reports zero")
	_check(_close(sheet.get_part_health(6), 0.0), "an out-of-range part id reports zero")
	_check(_close(sheet.get_part_max_health(-2), 0.0), "a negative part id reports zero")
	_check(not sheet.apply_damage(9, 5.0), "damage to an unknown part fails")
	_check(not sheet.apply_damage(-1, 5.0), "damage to a negative part fails")
	_check(not sheet.set_destination(Vector3(NAN, 0.0, 0.0)), "a nan destination fails")
	_check(sheet.is_configured() and sheet.get_character_id() == 1, "bad ids did not disturb the sheet")

	if failures == 0:
		print("Character sheet: all checks passed")
	quit(1 if failures else 0)
