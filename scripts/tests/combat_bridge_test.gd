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


# What one rank-1 unarmed blow costs a sheet that cannot answer it: 8 damage x 1/200 of
# trained rank, over a fresh sheet's Toughness of 1 (so / 1.05). The arithmetic itself is
# pinned in C++ (weapon_combat); what belongs here is that the bridge reports the typed
# number rather than the placeholder's 0.04, and that a ready defence a defender can pay
# for is worth everything - so an opening swing is turned aside whole, and the blow that
# lands is the one that met a defence already spent on somebody else.
const TYPED_BLOW := 0.04 / 1.05

# What any answer costs the sheet that makes it, whether it is a block or a dodge.
const DEFENCE_COST := 8.0

# The hand a shield is raised in: the C++ `Slot::LeftHand`.
const SLOT_LEFT_HAND := 5

# Every key a performed swing is expected to report, the new one included: the log is
# built on `defense_action`, and the popups and clips are built on the rest, so an event
# that loses a field is a bridge bug rather than a presentation problem.
const EVENT_KEYS := ["actor_id", "target_id", "part", "damage", "prevented", "attack_cooldown",
	"nullified", "technique_id", "attacker_item", "defender_item", "defense_action"]


func _defense_of(event: Dictionary) -> String:
	# Guarded on purpose: a bridge that never grew the key has to fail as an assertion in
	# this suite, not as a script error that ends the run before the rest of it is counted.
	if not event.has("defense_action"):
		_check(false, "the event carries a defense_action key")
		return "<missing>"
	return String(event["defense_action"])


func _combat():
	return ClassDB.instantiate(&"OpenCombat")


func _sheet(id: int, sheet_name: String) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(id, sheet_name, 0, 1, 0), "the %s sheet configures" % sheet_name)
	return sheet


func _parts_hit(sheet: OpenCharacter) -> Array:
	# [how many parts moved, what the one mover lost], so a caller can say "one part, this
	# amount" without knowing which part the engine drew.
	var changed := 0
	var amount := 0.0
	for part in range(6):
		var lost: float = sheet.get_part_max_health(part) - sheet.get_part_health(part)
		if not _close(lost, 0.0):
			changed += 1
			amount = lost
	return [changed, amount]


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
	# The opening swing happens and is turned aside. A fresh sheet has a ready dodge and
	# 100 stamina to pay for it, and a paid answer is worth everything it can do, so the
	# blow does nothing at all - which the bridge can only show through the two bars that
	# moved, because nothing else about the exchange is visible from here.
	_check(_close(target.get_health(), 100.0), "a ready, paid defence turns the opening swing aside")
	_check(_close(player.get_health(), 100.0), "the attacker was not hurt by swinging")
	_check(player.get_stamina() > 92.0 and player.get_stamina() < 93.0,
		"the swing paid 8 stamina and only this frame's combat regen came back")
	_check(target.get_stamina() > 92.0 and target.get_stamina() < 93.0,
		"and so did the defence that turned it aside, unasked")
	_check(_attack_target_of(target) == 0, "the defender never took an attack order itself")
	_check(combat.is_in_combat(2), "the defender is in the fight without ordering anything")
	var parts: Array = _parts_hit(target)
	_check(int(parts[0]) == 0, "a turned-aside swing wounds no body part at all")

	# The defence is one clock whatever it answers with, so the next attacker meets a
	# sheet that has just spent its answer: this blow is unanswered, and it arrives at the
	# typed amount, charged to exactly one body part.
	var second_attacker := _sheet(4, "Second Attacker")
	_check(combat.add_character(second_attacker, Vector3(1.0, 0.0, 0.8)),
		"an attacker standing inside the same reach registers")
	_check(combat.issue_attack(4, 2), "and it is ordered at the same target")
	var target_stamina_before: float = target.get_stamina()
	_check(combat.step(0.01), "the next frame is accepted")
	_check(_close(100.0 - target.get_health(), TYPED_BLOW),
		"the blow the spent defence could not answer costs the typed amount")
	var landed: Array = _parts_hit(target)
	_check(int(landed[0]) == 1, "exactly one body part took the hit")
	_check(_close(float(landed[1]), TYPED_BLOW), "and that part lost exactly the typed amount")
	_check(_close(second_attacker.get_stamina(), 100.0 - DEFENCE_COST + 0.05, 0.02),
		"the second attacker paid 8 for its own swing and nothing else")
	_check(target.get_stamina() > target_stamina_before,
		"and nothing at all is charged for an answer the clock refused")
	_check(combat.remove_character(4), "the extra attacker is stood down, so the fight below is one on one")

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
	_check(_close(target.get_health(), 100.0 - TYPED_BLOW), "and the old target is not hit again")


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
	# landed blows is not a coincidence one can reach by chance.
	var first := _swing_parts(12)
	var second := _swing_parts(12)
	_check(first.size() == 12 and second.size() == 12, "both sessions landed twelve blows")
	_check(first != second, "two OpenCombat objects do not draw the same body-part sequence")


func _swing_parts(swing_count: int) -> Array:
	# Two attackers on one target, which is not decoration: a single rank-1 attacker and a
	# single rank-1 defender refresh on the same clock, so every blow it throws is answered
	# and the draw is never spent. With two, the target answers one and the other lands, so
	# each landed blow is one draw of one body part, which is what this is collecting.
	var combat = _combat()
	var first_attacker := _sheet(1, "Seeded Attacker")
	var second_attacker := _sheet(4, "Seeded Attacker Two")
	var target := _sheet(2, "Seeded Target")
	_check(combat.add_character(first_attacker, Vector3.ZERO), "the first attacker registers")
	_check(combat.add_character(second_attacker, Vector3(0.2, 0.0, 0.0)),
		"the second attacker registers inside the same reach")
	_check(combat.add_character(target, Vector3(1.0, 0.0, 0.0)), "the target registers inside reach")
	_check(combat.issue_attack(1, 2), "the first attack is ordered")
	_check(combat.issue_attack(4, 2), "and the second")

	var drawn: Array = []
	var frames := 0
	while drawn.size() < swing_count and frames < 60:
		frames += 1
		var was: Array = []
		for part in range(6):
			was.append(target.get_part_health(part))
		# A rank-1 clock is 1/0.214 s, so one 5 s frame is one swing each: one answered,
		# one not.
		_check(combat.step(5.0), "a frame long enough for both swings runs")
		for part in range(6):
			if not _close(float(was[part]), target.get_part_health(part)):
				drawn.append(part)
	return drawn


func _swings_carry_their_clock() -> void:
	var combat = _combat()
	var hitter := _sheet(1, "Hitter")
	var foe := _sheet(2, "Foe")
	_check(combat.add_character(hitter, Vector3.ZERO), "the hitter registers")
	_check(combat.add_character(foe, Vector3(1.0, 0.0, 0.0)), "the foe registers in reach")
	_check(combat.issue_attack(1, 2), "the fight is on")
	# The swing happens on the first frame; the clock keeps ticking for frames after it;
	# the drain comes last. What the event carries is the duration the attempt started,
	# never the partially elapsed remainder - a clip scaled to that would run late.
	# Nothing may read the live clock (the bridge keeps no cooldown getter, by design),
	# so the oracle is an identical second fight drained the frame it swung: equal
	# durations prove the late report was not wearing down with the clock.
	combat.step(0.1)
	combat.step(0.4)
	var events: Array = combat.take_combat_events()
	_check(events.size() == 1, "one performed swing waits in the queue")
	if events.size() != 1:
		return
	_check(events[0].has("attack_cooldown"), "and the event carries its own clock")
	var late: float = float(events[0]["attack_cooldown"])
	_check(late > 0.0 && is_finite(late), "a positive finite duration: %s" % str(late))
	_check(combat.take_combat_events().is_empty(), "a drained queue is not a replay")

	var fresh = _combat()
	var hitter_b := _sheet(1, "Hitter")
	var foe_b := _sheet(2, "Foe")
	fresh.add_character(hitter_b, Vector3.ZERO)
	fresh.add_character(foe_b, Vector3(1.0, 0.0, 0.0))
	_check(fresh.issue_attack(1, 2), "the oracle fight is on")
	fresh.step(0.1)
	var now: Array = fresh.take_combat_events()
	_check(now.size() == 1, "the oracle swing is reported at once")
	if now.size() == 1:
		var same_clock := _close(late, float(now[0]["attack_cooldown"]), 0.0001)
		var note := "the late drain reports the whole duration, not the four and a half "
		note += "seconds left: %s against %s" % [str(late), str(float(now[0]["attack_cooldown"]))]
		_check(same_clock, note)


func _events_name_the_defence_that_ran() -> void:
	# The field exists because no number in an event can be read back into an answer: a
	# zero-damage blow might be a dodge, a full block or armour, and a blow that prevented
	# nothing might be a worthless block or no defence at all. The bridge's part is to say
	# which one C++ decided, in the one spelling the log switches on.
	#
	# --- dodge: a fresh unarmed sheet's first answer, paid for in full.
	var dodge_fight = _combat()
	var hitter := _sheet(1, "Dodge Hitter")
	var dodger := _sheet(2, "Dodge Dodger")
	_check(dodge_fight.add_character(hitter, Vector3.ZERO), "the attacker registers")
	_check(dodge_fight.add_character(dodger, Vector3(1.0, 0.0, 0.0)), "the defender registers in reach")
	_check(dodge_fight.issue_attack(1, 2), "the dodge fight is ordered")
	_check(dodge_fight.step(0.01), "a frame runs")
	var dodged: Array = dodge_fight.take_combat_events()
	_check(dodged.size() == 1, "one performed swing is one event, got %d" % dodged.size())
	if dodged.size() == 1:
		_check(_defense_of(dodged[0]) == "dodge", "the paid answer is reported as a dodge")
		# The tempting guess, pinned beside the fact it would get wrong: this is the
		# zero-damage event that *was* a defence.
		_check(bool(dodged[0]["nullified"]) and _close(float(dodged[0]["damage"]), 0.0),
			"and it is the nullified zero, as a dodge should be")
		_check(float(dodged[0]["prevented"]) > 0.0, "and it prevented the whole of the blow")
	_check(dodge_fight.take_combat_events().is_empty(), "the dodge queue is drained once")

	# --- block: the starter's own shield, raised with the ordinary bridge call.
	var block_fight = _combat()
	var attacker := _sheet(1, "Shield Attacker")
	var guard := _sheet(2, "Shield Guard")
	_check(block_fight.add_character(attacker, Vector3.ZERO), "the shield attacker registers")
	_check(block_fight.add_character(guard, Vector3(1.0, 0.0, 0.0)), "the guard registers in reach")
	_check(block_fight.setup_starter_player(2), "the guard is given the starter kit by C++")
	var shield_id := 0
	var shield_slots: Array = []
	for row in guard.get_equipment_snapshot():
		if String(row["model_key"]) == "Shield_Round":
			shield_id = int(row["item_id"])
			shield_slots = row["slots"]
	_check(shield_id != 0, "the starter kit owns a round shield the snapshot can name")
	if shield_slots.is_empty():
		_check(guard.equip_item(shield_id, SLOT_LEFT_HAND), "the shield is raised in the left hand")
	else:
		_check(shield_slots.has(SLOT_LEFT_HAND), "the shield already holds the left hand")
	_check(block_fight.issue_attack(1, 2), "the block fight is ordered")
	_check(block_fight.step(0.01), "a frame runs")
	var blocked: Array = block_fight.take_combat_events()
	_check(blocked.size() == 1, "one performed swing is one event, got %d" % blocked.size())
	if blocked.size() == 1:
		_check(_defense_of(blocked[0]) == "block", "a shield answer is reported as a block")
		_check(int(blocked[0]["defender_item"]) == shield_id, "and it still names the item it blocked with")
		_check(float(blocked[0]["prevented"]) > 0.0, "the board prevented something")
		_check(float(blocked[0]["damage"]) > 0.0, "and part of the blow still arrived")
	_check(block_fight.take_combat_events().is_empty(), "the block queue is drained once")

	# --- none: one defence clock, spent by the first of two attackers in the same frame.
	var spent = _combat()
	# Registration order is swing order, so the two attackers are placed on purpose: the one
	# named first is the one the target answers, and the second is the one it cannot.
	var first := _sheet(1, "First Attacker")
	var second := _sheet(4, "Second Attacker")
	var shared := _sheet(2, "Shared Target")
	_check(spent.add_character(first, Vector3.ZERO), "the first attacker registers")
	_check(spent.add_character(second, Vector3(0.2, 0.0, 0.0)),
		"the second attacker registers inside the same reach")
	_check(spent.add_character(shared, Vector3(1.0, 0.0, 0.0)), "the target registers in reach")
	_check(spent.issue_attack(1, 2), "the first attack is ordered")
	_check(spent.issue_attack(4, 2), "and the second")
	_check(spent.step(0.01), "one frame carries both swings")
	var pair: Array = spent.take_combat_events()
	_check(pair.size() == 2, "two performed swings are two events, got %d" % pair.size())
	if pair.size() == 2:
		_check(int(pair[0]["actor_id"]) == 1 and int(pair[1]["actor_id"]) == 4,
			"the events stay in the order the swings happened")
		_check(_defense_of(pair[0]) == "dodge", "the first blow met the defence that was ready")
		_check(_defense_of(pair[1]) == "none", "the second met the same clock cooling")
		_check(float(pair[1]["damage"]) > 0.0, "and so the second blow arrived")
	for key in EVENT_KEYS:
		_check(pair[0].has(key), "the event still carries %s" % key)
	_check(spent.take_combat_events().is_empty(), "a drained queue is not a replay")


func _run() -> void:
	_registration_and_safe_defaults()
	_end_to_end_fight()
	_swings_carry_their_clock()
	_events_name_the_defence_that_ran()
	_lifetime_and_safety()
	_seeding_varies_per_session()

	if failures == 0:
		print("Combat bridge: all checks passed")
	quit(1 if failures else 0)
