extends SceneTree

# Task 5 bridge test. OpenCombat is a thin adapter over open::CombatSession: it
# converts Godot types, keeps the OpenCharacter objects alive and forwards intent.
# Every combat answer - reach, damage, in_combat, cooldowns - must come from C++.
#
# The class is reached through ClassDB with an existence guard, so a missing
# registration fails as an assertion in this test instead of a GDScript parse error.

var failures := 0


func _initialize() -> void:
	if not ClassDB.class_exists(&"OpenCombat"):
		printerr("FAIL: OpenCombat is not registered by the extension")
		quit(1)
		return
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _combat():
	return ClassDB.instantiate(&"OpenCombat")


func _sheet(id: int, sheet_name: String) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(id, sheet_name, 0, 1, 0), "the %s sheet configures" % sheet_name)
	return sheet


func _attack_target_of(sheet: OpenCharacter) -> int:
	if not sheet.has_method("get_attack_target_id"):
		_check(false, "the sheet bridge exposes get_attack_target_id")
		return -1
	return int(sheet.call("get_attack_target_id"))


func _registration_and_safe_defaults() -> void:
	var combat = _combat()
	_check(combat != null, "OpenCombat instantiates through ClassDB")
	_check(not combat.is_in_combat(1), "an empty coordinator has nobody in combat")
	_check(combat.velocity_at(1) == Vector3.ZERO, "an empty coordinator does not move anybody")
	_check(combat.step(0.0), "an empty coordinator accepts a valid empty frame")
	_check(not combat.step(-1.0), "an empty coordinator refuses negative time")
	_check(not combat.step(NAN), "an empty coordinator refuses nan time")
	_check(not combat.step(INF), "an empty coordinator refuses infinite time")
	_check(not combat.remove_character(0), "removing id zero fails")
	_check(not combat.remove_character(-1), "removing a negative id fails")
	_check(not combat.remove_character(99), "removing an unregistered id fails")
	_check(not combat.set_position(99, Vector3.ONE), "an unregistered position fails")
	_check(not combat.set_position(-3, Vector3.ONE), "a negative id position fails")
	_check(not combat.issue_attack(99, 98), "an attack between unregistered ids fails")
	_check(not combat.issue_attack(-1, 98), "a negative actor id is refused")
	_check(not combat.issue_attack(98, 0), "a zero target id is refused")
	_check(not combat.is_in_combat(-2), "a negative id is never in combat")
	_check(combat.velocity_at(-4) == Vector3.ZERO, "a negative id never moves")

	_check(not combat.add_character(null, Vector3.ZERO), "a null sheet is refused")
	_check(not combat.add_character(OpenCharacter.new(), Vector3.ZERO), "an unconfigured sheet is refused")

	var placed := _sheet(11, "Placed")
	_check(not combat.add_character(placed, Vector3(NAN, 0.0, 0.0)), "a nan position is refused")
	_check(not combat.add_character(placed, Vector3(INF, 0.0, 0.0)), "an infinite position is refused")
	_check(not combat.add_character(placed, Vector3(0.0, 0.0, NAN)), "a nan height is refused")
	_check(combat.add_character(placed, Vector3(1.0, 0.0, 0.0)),
		"the sheet still registers after the refused positions")
	_check(not combat.add_character(placed, Vector3(2.0, 0.0, 0.0)), "registering the same sheet twice fails")
	_check(not combat.set_position(11, Vector3(NAN, 0.0, 0.0)), "a nan position update is refused")
	_check(not combat.set_position(11, Vector3(0.0, INF, 0.0)), "an infinite height update is refused")

	var twin_a := _sheet(12, "Twin A")
	var twin_b := _sheet(12, "Twin B")
	_check(combat.add_character(twin_a, Vector3.ZERO), "the first twin registers")
	_check(not combat.add_character(twin_b, Vector3(1.0, 0.0, 0.0)), "a duplicate character id is refused")

	_check(placed.set_destination(Vector3(4.0, 0.0, 0.0)), "the registered sheet takes a move order")
	var walk: Vector3 = combat.velocity_at(11)
	_check(_close(walk.x, 4.5) and _close(walk.y, 0.0) and _close(walk.z, 0.0),
		"the refused calls left the registration and the C++ velocity alone")
	_check(_attack_target_of(placed) == 0, "a registered sheet with only a move order attacks nobody")

	var wide_combat = _combat()
	var wide := OpenCharacter.new()
	_check(wide.configure((1 << 32) + 1, "Wide", 0, 1, 0), "a full-width character id configures")
	_check(wide_combat.add_character(wide, Vector3.ZERO), "a full-width id registers")
	_check(not wide_combat.is_in_combat((1 << 32) + 1), "the wide id answers queries")
	_check(wide_combat.velocity_at((1 << 32) + 1) == Vector3.ZERO, "and does not invent motion")
	_check(not wide_combat.add_character(_sheet(13, "Near"), Vector3(NAN, 0.0, 0.0)),
		"a wide-id coordinator still refuses a bad position")


func _end_to_end_fight() -> void:
	var combat = _combat()
	var player := _sheet(1, "Player")
	var target := _sheet(2, "Target")
	_check(combat.add_character(player, Vector3.ZERO), "the player registers at the origin")
	_check(combat.add_character(target, Vector3(1.0, 0.0, 0.0)), "the target registers one step away")
	_check(not combat.is_in_combat(1), "nobody is fighting before an order")
	_check(combat.velocity_at(1) == Vector3.ZERO, "and nobody moves")

	_check(combat.issue_attack(1, 2), "an attack order by id is accepted")
	_check(combat.is_in_combat(1), "the attacker is in combat")
	_check(combat.is_in_combat(2), "the passive target is in combat too")
	_check(_attack_target_of(player) == 2, "the sheet reports the target C++ is holding")
	_check(_attack_target_of(target) == 0, "the defender ordered nobody")
	_check(combat.velocity_at(1) == Vector3.ZERO, "inside reach C++ asks for no walk")
	_check(not combat.issue_attack(1, 1), "an attack on itself is refused")
	_check(not combat.issue_attack(1, 99), "an attack on an unregistered target is refused")
	_check(_attack_target_of(player) == 2, "the refused orders left the intent alone")

	_check(combat.step(0.0), "a zero-length frame is accepted")
	_check(_close(target.get_health(), 100.0), "a zero-length frame swings at nothing")
	_check(not combat.step(NAN), "nan time is refused")
	_check(not combat.step(-0.5), "negative time is refused")
	_check(_close(target.get_health(), 100.0), "a refused frame damaged nobody")

	_check(combat.step(0.01), "one physics frame is accepted")
	_check(_close(target.get_health(), 99.96), "the C++ swing cost the target 0.04 hp")
	var changed_parts := 0
	for part in range(6):
		var lost: float = target.get_part_max_health(part) - target.get_part_health(part)
		if not _close(lost, 0.0):
			changed_parts += 1
			_check(_close(lost, 0.04), "the stricken part lost exactly 0.04")
	_check(changed_parts == 1, "exactly one body part took the hit")
	_check(_close(player.get_health(), 100.0), "the attacker was not hurt by swinging")
	_check(player.get_stamina() > 92.0 and player.get_stamina() < 93.0,
		"the swing paid 8 stamina and only this frame's combat regen came back")
	_check(_attack_target_of(target) == 0, "the defender never took an attack order itself")
	_check(combat.is_in_combat(2), "the defender is in the fight without ordering anything")

	_check(player.set_destination(Vector3(6.0, 0.0, 0.0)), "a floor-style move order goes through the sheet")
	_check(_attack_target_of(player) == 0, "the move order cancelled the attack")
	_check(not combat.is_in_combat(1), "the attacker left the fight")
	_check(not combat.is_in_combat(2), "and so did the defender")
	var walk: Vector3 = combat.velocity_at(1)
	_check(_close(walk.x, 4.5) and _close(walk.y, 0.0) and _close(walk.z, 0.0),
		"the coordinator publishes ordinary move velocity again")

	var third := _sheet(3, "Third")
	_check(combat.add_character(third, Vector3(0.5, 0.0, 0.0)), "a third sheet registers inside reach")
	_check(combat.issue_attack(1, 3), "the player can be re-ordered at another id")
	_check(combat.is_in_combat(1) and combat.is_in_combat(3), "the new pair is in combat")
	_check(combat.velocity_at(1) == Vector3.ZERO, "the retarget stands inside reach too")
	_check(combat.step(0.01), "the next frame is accepted")
	_check(_close(third.get_health(), 100.0), "retargeting does not bypass the attack cooldown")
	_check(_close(target.get_health(), 99.96), "and the old target is not hit again")


func _lifetime_and_safety() -> void:
	var pursued = _combat()
	var hunter := _sheet(1, "Hunter")
	var quarry := _sheet(2, "Quarry")
	_check(pursued.add_character(hunter, Vector3.ZERO), "the hunter registers")
	_check(pursued.add_character(quarry, Vector3(30.0, 0.0, 0.0)), "the quarry registers far away")
	_check(pursued.issue_attack(1, 2), "an out-of-range order is accepted")
	_check(pursued.is_in_combat(1), "pursuit counts as being in combat")
	_check(pursued.is_in_combat(2), "and the pursued sheet is in combat as well")
	var chase: Vector3 = pursued.velocity_at(1)
	_check(_close(chase.x, 4.5) and _close(chase.y, 0.0) and _close(chase.z, 0.0),
		"the coordinator publishes C++ pursuit velocity")
	var quarry_hp := quarry.get_health()
	_check(pursued.remove_character(2), "the target is removed")
	_check(_attack_target_of(hunter) == 0, "removal reconciled the stale order away")
	_check(not pursued.is_in_combat(1), "the hunter is out of the fight")
	_check(not pursued.is_in_combat(2), "a removed id answers as nobody")
	_check(pursued.velocity_at(1) == Vector3.ZERO, "no stale pursuit velocity survives a removal")
	_check(_close(quarry_hp - quarry.get_health(), 0.0), "removing a sheet damaged nothing")
	_check(not pursued.remove_character(2), "removing the same id twice fails")

	var kills = _combat()
	var attacker := _sheet(1, "Attacker")
	var victim := _sheet(2, "Victim")
	_check(kills.add_character(attacker, Vector3.ZERO), "the attacker registers")
	_check(kills.add_character(victim, Vector3(1.0, 0.0, 0.0)), "the victim registers inside reach")
	_check(kills.issue_attack(1, 2), "the attacker is ordered")
	_check(victim.apply_damage(1, 50.0), "the bridge kills the victim through the torso")
	_check(not victim.is_alive(), "the victim is dead")
	_check(not kills.issue_attack(1, 2), "an order on a dead sheet is refused")
	var victim_hp := victim.get_health()
	var attacker_hp := attacker.get_health()
	_check(kills.step(1.0), "the frame after the kill is accepted")
	_check(_close(victim_hp - victim.get_health(), 0.0), "a dead sheet takes no more damage")
	_check(_attack_target_of(attacker) == 0, "the attacker's order was reconciled away")
	_check(not kills.is_in_combat(1), "the attacker is no longer fighting")
	_check(_close(attacker_hp - attacker.get_health(), 0.0), "nothing was swung back at the attacker")

	var dead_attacker = _combat()
	var killer := _sheet(1, "Killer")
	var prey := _sheet(2, "Prey")
	_check(dead_attacker.add_character(killer, Vector3.ZERO), "the killer registers")
	_check(dead_attacker.add_character(prey, Vector3(1.0, 0.0, 0.0)), "the prey registers inside reach")
	_check(dead_attacker.issue_attack(1, 2), "the killer is ordered")
	_check(killer.apply_damage(1, 50.0), "the bridge kills the attacker instead")
	_check(_attack_target_of(killer) == 0, "death cleared its own order")
	_check(not dead_attacker.issue_attack(1, 2), "a dead actor cannot be re-ordered")
	var prey_hp := prey.get_health()
	_check(dead_attacker.step(1.0), "the next frame is accepted")
	_check(_close(prey_hp - prey.get_health(), 0.0), "a dead actor swung nothing")
	_check(not dead_attacker.is_in_combat(2), "the prey is out of the fight")

	var held = _combat()
	var keeper := _sheet(1, "Keeper")
	_check(held.add_character(keeper, Vector3.ZERO), "the keeper registers")
	var dropped := _sheet(2, "Dropped")
	_check(held.add_character(dropped, Vector3(1.0, 0.0, 0.0)), "the sheet to be dropped registers")
	_check(held.issue_attack(1, 2), "and gets ordered")
	dropped = null
	_check(held.is_in_combat(2), "the coordinator still holds the sheet nobody else references")
	_check(_attack_target_of(keeper) == 2, "and still resolves its live order against it")
	_check(held.step(0.01), "the frame runs against the retained sheet")
	_check(held.is_in_combat(2), "the fight is still live after that frame")
	_check(held.remove_character(2), "the retained sheet can be released by id")
	_check(_attack_target_of(keeper) == 0, "and its attacker is reconciled")
	_check(held.remove_character(1), "the last character can be removed too")
	# Reaching the next line is itself the check: releasing the last character and
	# then the coordinator, in that order, must not touch a freed sheet.
	keeper = null
	held = null

	var frozen = _combat()
	var mover := _sheet(1, "Mover")
	_check(frozen.add_character(mover, Vector3(0.0, 0.0, 0.0)), "the mover registers")
	_check(mover.set_destination(Vector3(10.0, 0.0, 0.0)), "the mover takes a move order")
	var before: Vector3 = frozen.velocity_at(1)
	_check(not frozen.set_position(1, Vector3(NAN, 0.0, 0.0)), "a nan position update is refused")
	_check(not frozen.set_position(1, Vector3(0.0, INF, 0.0)), "an infinite position update is refused")
	_check(frozen.velocity_at(1) == before, "the refused update kept the previous position")
	for bad in [-1.0, NAN, INF, -INF]:
		_check(not frozen.step(bad), "step refuses time %s" % bad)
	_check(frozen.velocity_at(1) == before, "refused frames changed nothing")
	_check(_attack_target_of(mover) == 0, "and no bad frame invented an attack order")
	# Park the mover past its destination: from (0,0,0) the walk points +x, from
	# (12,0,0) it must point back at -x. Only a position that was really stored can
	# flip the sign, so a set_position that silently no-oped cannot pass here.
	_check(frozen.set_position(1, Vector3(12.0, 0.0, 0.0)), "a valid position update is accepted")
	var resumed: Vector3 = frozen.velocity_at(1)
	_check(_close(resumed.x, -4.5), "the stored position, not the old one, feeds the C++ move math")


func _seeding_varies_per_session() -> void:
	# Two coordinator objects fight the same way from the same fixture. If the
	# default constructor ever seeds from a constant - a fixed number, or a shared
	# engine - both body-part sequences come out identical. Six parts over twelve
	# swings is not a coincidence one can reach by chance.
	var first := _swing_parts(12)
	var second := _swing_parts(12)
	_check(first.size() == 12 and second.size() == 12, "both sessions swung twelve times")
	_check(first != second, "two OpenCombat objects do not draw the same body-part sequence")


func _swing_parts(swing_count: int) -> Array:
	var combat = _combat()
	var attacker := _sheet(1, "Seeded Attacker")
	var target := _sheet(2, "Seeded Target")
	_check(combat.add_character(attacker, Vector3.ZERO), "the attacker registers")
	_check(combat.add_character(target, Vector3(1.0, 0.0, 0.0)), "the target registers inside reach")
	_check(combat.issue_attack(1, 2), "the attack is ordered")

	var drawn: Array = []
	for swing in range(swing_count):
		var was: Array = []
		for part in range(6):
			was.append(target.get_part_health(part))
		# A rank-1 clock is 1/0.214 s, so one 5 s frame is exactly one swing.
		_check(combat.step(5.0), "a frame long enough for one swing runs")
		for part in range(6):
			if not _close(float(was[part]), target.get_part_health(part)):
				drawn.append(part)
	return drawn


func _run() -> void:
	_registration_and_safe_defaults()
	_end_to_end_fight()
	_lifetime_and_safety()
	_seeding_varies_per_session()

	if failures == 0:
		print("Combat bridge: all checks passed")
	quit(1 if failures else 0)
