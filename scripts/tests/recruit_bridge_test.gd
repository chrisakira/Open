extends SceneTree

# Task 3 recruit bridge test. `OpenCharacter` and `OpenCombat` stay thin adapters:
# they convert Godot values and forward intent, and the answer to every recruit
# question - who may be asked, what the check says, whether anyone joined - comes
# back from C++.
#
# Every new method is reached through `has_method` and `call`, so a bridge that has
# not grown the door fails as a counted assertion in this suite instead of a script
# error that ends the run before the rest of it is counted.

var failures := 0


func _initialize() -> void:
	if not ClassDB.class_exists(&"OpenCombat"):
		printerr("FAIL: OpenCombat is not registered by the extension")
		quit(1)
		return
	if not ClassDB.class_exists(&"OpenCharacter"):
		printerr("FAIL: OpenCharacter is not registered by the extension")
		quit(1)
		return
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# The two recruit conditions, as the bridge names them: 0 always fails, 1 always
# succeeds. The ints are the contract the scene passes; the enum stays in C++.
const ALWAYS_FAIL := 0
const ALWAYS_SUCCEED := 1


func _combat():
	return ClassDB.instantiate(&"OpenCombat")


func _sheet(id: int, sheet_name: String, faction: int = 0) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(id, sheet_name, faction, 1, 0), "the %s sheet configures" % sheet_name)
	return sheet


# --- guarded reads and commands ---------------------------------------------------
#
# One helper per new door. Each names the method the bridge is expected to have, so a
# failure line says which door is missing rather than which assertion was unreachable.

func _method_on(object: Object, method: String, label: String) -> bool:
	if not object.has_method(method):
		_check(false, "%s exposes %s" % [label, method])
		return false
	return true


func _recruitable_of(sheet: OpenCharacter) -> bool:
	if not _method_on(sheet, "is_recruitable", "the sheet bridge"):
		return false
	return bool(sheet.call("is_recruitable"))


func _set_recruitable(sheet: OpenCharacter, value: bool) -> bool:
	if not _method_on(sheet, "set_recruitable", "the sheet bridge"):
		return false
	return bool(sheet.call("set_recruitable", value))


func _recruit_condition_of(sheet: OpenCharacter) -> int:
	if not _method_on(sheet, "get_recruit_condition", "the sheet bridge"):
		return -1
	return int(sheet.call("get_recruit_condition"))


func _set_recruit_condition(sheet: OpenCharacter, condition: int) -> bool:
	if not _method_on(sheet, "set_recruit_condition", "the sheet bridge"):
		return false
	return bool(sheet.call("set_recruit_condition", condition))


func _in_squad_of(sheet: OpenCharacter) -> bool:
	if not _method_on(sheet, "is_in_squad", "the sheet bridge"):
		return false
	return bool(sheet.call("is_in_squad"))


func _recruit_target_of(sheet: OpenCharacter) -> int:
	if not _method_on(sheet, "get_recruit_target_id", "the sheet bridge"):
		return -1
	return int(sheet.call("get_recruit_target_id"))


# The one door the scene has to its own squad: seat the founder.
func _found_squad(combat, actor_id: int) -> bool:
	if not _method_on(combat, "found_squad", "the coordinator"):
		return false
	return bool(combat.call("found_squad", actor_id))


# Whose ask this sheet answered, or 0 when nobody has answered to anyone yet.
func _answered_to_of(sheet: OpenCharacter) -> int:
	if not _method_on(sheet, "get_answered_to_id", "the sheet bridge"):
		return -1
	return int(sheet.call("get_answered_to_id"))


func _issue_recruit(combat, actor_id: int, target_id: int) -> bool:
	if not _method_on(combat, "issue_recruit", "the coordinator"):
		return false
	return bool(combat.call("issue_recruit", actor_id, target_id))


# --- the tests --------------------------------------------------------------------


func _safe_defaults() -> void:
	var bare := OpenCharacter.new()
	_check(not bare.is_configured(), "an unconfigured object says nothing")
	_check(not _recruitable_of(bare), "an unconfigured sheet is not recruitable")
	_check(_recruit_condition_of(bare) == ALWAYS_FAIL, "an unconfigured sheet always-fails by default")
	_check(not _in_squad_of(bare), "an unconfigured sheet is in nobody's squad")
	_check(_recruit_target_of(bare) == 0, "an unconfigured sheet is asking nobody")
	_check(not _set_recruitable(bare, true), "an unconfigured sheet takes no flag")
	_check(not _set_recruit_condition(bare, ALWAYS_SUCCEED), "an unconfigured sheet takes no condition")

	var sheet := _sheet(1, "Flagged")
	_check(not _recruitable_of(sheet), "a fresh sheet starts unrecruitable")
	_check(_recruit_condition_of(sheet) == ALWAYS_FAIL, "a fresh sheet starts with the failing condition")
	_check(not _in_squad_of(sheet), "a fresh sheet belongs to nobody")
	_check(_recruit_target_of(sheet) == 0, "a fresh sheet asks nobody")
	_check(_set_recruitable(sheet, true), "a living sheet takes the flag")
	_check(_recruitable_of(sheet), "and reports it back")
	_check(_set_recruit_condition(sheet, ALWAYS_SUCCEED), "a living sheet takes the succeeding condition")
	_check(_recruit_condition_of(sheet) == ALWAYS_SUCCEED, "and reports that condition back")
	_check(_set_recruit_condition(sheet, ALWAYS_FAIL), "a living sheet takes the failing condition too")
	_check(_recruit_condition_of(sheet) == ALWAYS_FAIL, "and reports that one back")
	_check(not _set_recruit_condition(sheet, 2), "condition 2 is not a condition")
	_check(not _set_recruit_condition(sheet, -1), "a negative condition is refused")
	_check(not _set_recruit_condition(sheet, 99), "an unimagined condition is refused")
	_check(_recruit_condition_of(sheet) == ALWAYS_FAIL, "a refused int left the condition alone")
	_check(not _in_squad_of(sheet), "and no reader ever said it joined")

	_check(sheet.apply_damage(1, 200.0), "the sheet can be killed through the bridge")
	_check(not sheet.is_alive(), "dead")
	_check(not _set_recruitable(sheet, false), "a corpse is unasked and stays so")
	_check(not _set_recruit_condition(sheet, ALWAYS_SUCCEED), "a corpse takes no condition")
	_check(_recruitable_of(sheet), "the flag it died with is still on the body")
	_check(_recruit_condition_of(sheet) == ALWAYS_FAIL, "and so is its condition")


func _no_door_forwards_to_membership_or_faction() -> void:
	var sheet := _sheet(1, "Guarded")
	# Membership and faction are what an answered ask does in C++. A bridge that let a
	# script write them would let a popup declare somebody a member of its own accord.
	_check(not sheet.has_method("set_in_squad"), "the sheet bridge cannot put anyone in a squad")
	_check(not sheet.has_method("set_faction"), "the sheet bridge cannot change a faction")
	_check(not sheet.has_method("apply_recruit_success"), "the sheet bridge cannot apply a join")
	_check(not sheet.has_method("apply_recruit_failure"), "the sheet bridge cannot apply a refusal")


func _far_ask_walks_and_the_close_one_answers() -> void:
	var combat = _combat()
	var player := _sheet(1, "Player")
	var jane := _sheet(2, "Jane")
	var enemy := _sheet(3, "Enemy", 1)
	_check(_set_recruitable(jane, true), "Jane is recruitable")
	_check(_set_recruit_condition(jane, ALWAYS_SUCCEED), "and Jane always says yes")
	_check(_set_recruitable(enemy, true), "the enemy is recruitable too")
	_check(combat.add_character(player, Vector3.ZERO), "the player registers at the origin")
	_check(combat.add_character(jane, Vector3(0.0, 0.0, 20.0)), "Jane registers far away")
	_check(combat.add_character(enemy, Vector3(0.0, 0.0, 1.0)), "the enemy registers in reach")

	_check(_issue_recruit(combat, 1, 2), "Jane is asked from far away")
	_check(_recruit_target_of(player) == 2, "the ask is held on the sheet")
	var walk: Vector3 = combat.velocity_at(1)
	_check(walk.z > 0.0 and _close(walk.y, 0.0), "the coordinator walks the recruiter toward Jane")
	_check(not _in_squad_of(jane), "a far ask has answered nothing yet")
	_check(_recruitable_of(jane), "and the flag stays unspent while the walk is on")
	_check(combat.step(0.01), "a frame runs out of reach")
	_check(not _in_squad_of(jane), "out of reach, nobody joined")
	_check(_recruitable_of(jane), "and the flag is still there")
	_check(_recruit_target_of(player) == 2, "and the ask is still held")

	# The recruiter stands where the coordinator said to stand, which is how the scene
	# closes a distance: positions are the caller's observations, never the session's.
	_check(combat.set_position(1, Vector3(0.0, 0.0, 19.0)), "the recruiter walks up to Jane")
	_check(combat.velocity_at(1) == Vector3.ZERO, "in reach the ask asks for no more walking")
	_check(combat.step(0.01), "a frame runs in reach")
	_check(_in_squad_of(jane), "Jane joined")
	_check(jane.get_faction() == 0, "and Jane stays Friendly")
	_check(not _recruitable_of(jane), "the flag is spent")
	_check(_recruit_target_of(player) == 0, "the recruiter was stood down")
	_check(not _in_squad_of(enemy), "the other sheet was never asked")
	_check(combat.velocity_at(1) == Vector3.ZERO, "and a stood-down recruiter walks on nothing")

	# The enemy's own ask, answered in reach by a condition that says no: nothing joins,
	# no faction moves, and the flag is spent anyway - one attempt.
	_check(combat.set_position(1, Vector3.ZERO), "the recruiter comes back for the next ask")
	_check(_issue_recruit(combat, 1, 3), "the enemy is asked in reach")
	_check(combat.step(0.01), "a frame runs in reach")
	_check(not _in_squad_of(enemy), "the answer was no")
	_check(enemy.get_faction() == 1, "and a refused Enemy is still Enemy")
	_check(not _recruitable_of(enemy), "a refusal spends the flag too")
	_check(not _issue_recruit(combat, 1, 3), "a spent flag is not askable twice")


func _asks_that_are_not_asks() -> void:
	var combat = _combat()
	var player := _sheet(1, "Player")
	var jane := _sheet(2, "Jane")
	var fallen := _sheet(3, "Fallen")
	_check(_set_recruitable(jane, true), "Jane is recruitable")
	_check(combat.add_character(player, Vector3.ZERO), "the player registers")
	_check(combat.add_character(jane, Vector3(1.0, 0.0, 0.0)), "Jane registers in reach")
	_check(combat.add_character(fallen, Vector3(2.0, 0.0, 0.0)), "a third sheet registers")
	_check(fallen.apply_damage(1, 200.0), "and then dies")

	_check(not _issue_recruit(combat, 1, 0), "a zero target is refused")
	_check(not _issue_recruit(combat, 1, -1), "a negative target is refused")
	_check(not _issue_recruit(combat, 1, 1), "asking self is refused")
	_check(not _issue_recruit(combat, 1, 99), "an unregistered target is refused")
	_check(not _issue_recruit(combat, 99, 2), "an unregistered actor is refused")
	_check(not _issue_recruit(combat, -1, 2), "a negative actor id is refused")
	_check(not _issue_recruit(combat, 3, 2), "a dead actor is refused")
	_check(not _issue_recruit(combat, 1, 3), "a dead target is refused")
	_check(_recruit_target_of(player) == 0, "every refusal left no order behind")

	# An unrecruitable sheet is refused on the flag, and faction is no filter: an Enemy
	# with the flag is asked like anyone else.
	var hostile := _sheet(4, "Hostile", 1)
	_check(combat.add_character(hostile, Vector3(1.0, 0.0, 1.0)), "an enemy registers in reach")
	_check(not _issue_recruit(combat, 1, 4), "an unrecruitable enemy is refused")
	_check(_recruit_target_of(player) == 0, "and the refusal minted nothing")
	_check(_set_recruitable(hostile, true), "the enemy takes the flag")
	_check(_issue_recruit(combat, 1, 4), "and becomes askable, faction intact")
	_check(_recruit_target_of(player) == 4, "the ask is held on the sheet")

	# Jane loses her flag, so the second ask is a refusal rather than a second join.
	_check(_set_recruitable(jane, false), "Jane can lose the flag through the bridge")
	_check(not _issue_recruit(combat, 1, 2), "a sheet without the flag is not askable")


func _the_founder_is_seated_and_no_answer_is_stamped() -> void:
	var combat = _combat()
	var founder := _sheet(1, "Founder", 1)
	var other := _sheet(2, "Other")
	_check(combat.add_character(founder, Vector3.ZERO), "the founder registers")
	_check(combat.add_character(other, Vector3(1.0, 0.0, 0.0)), "a second sheet registers")
	_check(_set_recruitable(founder, true), "the founder is askable, as any sheet may be")
	_check(_set_recruit_condition(founder, ALWAYS_SUCCEED), "and it carries the condition it was given")
	_check(not _in_squad_of(founder), "a fresh sheet is nobody's founder yet")
	_check(_answered_to_of(founder) == 0, "and it has answered to nobody")

	_check(_found_squad(combat, 1), "the coordinator seats its founder")
	_check(_in_squad_of(founder), "the founder is in the squad")
	_check(not _in_squad_of(other), "and only the founder is")
	_check(_answered_to_of(founder) == 0, "a seat is not an answer, so nobody was answered to")
	_check(founder.get_faction() == 1, "the seat keeps the faction it was configured with")
	_check(_recruitable_of(founder), "and the seat keeps the flag it was configured with")
	_check(_recruit_condition_of(founder) == ALWAYS_SUCCEED, "and it keeps the condition it was given")

	_check(_found_squad(combat, 1), "founding the seated founder again is accepted")
	_check(_in_squad_of(founder), "and it is still the one seat")

	_check(not _found_squad(combat, 99), "an absent id is not seated")
	_check(not _found_squad(combat, 0), "a zero id is not seated")
	_check(not _found_squad(combat, -1), "a negative id is not seated")
	_check(other.apply_damage(1, 200.0), "the second sheet dies")
	_check(not _found_squad(combat, 2), "a corpse is nobody's founder")
	_check(not _in_squad_of(other), "and refusing to seat a corpse seated nobody")


func _an_answer_remembers_who_it_was_answered_to() -> void:
	var combat = _combat()
	var recruiter := _sheet(1, "Recruiter")
	var yes := _sheet(2, "Yes")
	var no := _sheet(3, "No")
	_check(_set_recruitable(yes, true), "the first sheet is askable")
	_check(_set_recruit_condition(yes, ALWAYS_SUCCEED), "and it says yes")
	_check(_set_recruitable(no, true), "the second sheet is askable")
	_check(_set_recruit_condition(no, ALWAYS_FAIL), "and it says no")
	_check(combat.add_character(recruiter, Vector3.ZERO), "the recruiter registers")
	_check(combat.add_character(yes, Vector3(1.0, 0.0, 0.0)), "the yes registers in reach")
	_check(combat.add_character(no, Vector3(0.0, 0.0, 1.0)), "the no registers in reach too")
	_check(_answered_to_of(yes) == 0, "nobody has been answered to before the ask")

	_check(_issue_recruit(combat, 1, 2), "the yes is asked in reach")
	_check(combat.step(0.01), "a frame answers it")
	_check(_in_squad_of(yes), "it joined")
	_check(_answered_to_of(yes) == 1, "and its answer belongs to the recruiter who asked it")

	_check(_issue_recruit(combat, 1, 3), "the no is asked in reach")
	_check(combat.step(0.01), "a frame answers that too")
	_check(not _in_squad_of(no), "it refused, so nobody joined")
	_check(_answered_to_of(no) == 1, "and a refusal is still an answer to somebody")
	_check(not _recruitable_of(no), "a refusal spends the flag")

	_check(not _in_squad_of(recruiter), "and no answer seated the recruiter who asked")


func _run() -> void:
	_safe_defaults()
	_no_door_forwards_to_membership_or_faction()
	_the_founder_is_seated_and_no_answer_is_stamped()
	_an_answer_remembers_who_it_was_answered_to()
	_far_ask_walks_and_the_close_one_answers()
	_asks_that_are_not_asks()

	if failures == 0:
		print("Recruit bridge: all checks passed")
	quit(1 if failures else 0)
