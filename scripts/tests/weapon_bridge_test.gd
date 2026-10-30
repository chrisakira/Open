extends SceneTree

# Task 14a: the bridge's equipment, weapon and technique surface.
#
# Nothing in the scene can own an item yet - the production catalog is Task 15's and the
# real owned-instance round trips are Task 14b's - so this suite pins the *contract*: the
# shape of every row, safe answers from a sheet that has nothing, and commands that are
# refused without leaving anything half-applied. The rows are built by C++ from the sheet's
# own state, so what a script may learn here is what a sheet really is; the dictionary
# fixtures below are test-only and use the declared schema, never a fabricated item.
#
# The methods are reached through untyped handles and `has_method` guards, so a method the
# extension does not register fails as an assertion in this suite rather than as a parse
# error that hides every other check.

const SLOT_LEFT_HAND := 5
const SLOT_RIGHT_HAND := 6
const SLOT_CHEST := 1
const SLOT_COUNT := 20

# The technique id every sheet can name in 14a: there is no catalog to resolve it against,
# so it stands for "an entry the sequence has and the held item does not support".
const UNKNOWN_TECHNIQUE := 4242

var failures := 0


func _initialize() -> void:
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


# Untyped on purpose: a typed handle would make every not-yet-registered method below a
# parse error instead of a finding.
func _bare_sheet():
	return ClassDB.instantiate(&"OpenCharacter")


func _sheet(id: int):
	var sheet = _bare_sheet()
	_check(sheet.configure(id, "Bridge Fixture", 0, 1, 0), "the %d sheet configures" % id)
	return sheet


func _step(technique_id, skip_on_cooldown) -> Dictionary:
	return {"technique_id": technique_id, "skip_on_cooldown": skip_on_cooldown}


func _has_schema(dictionary: Dictionary, keys: Array) -> bool:
	for key in keys:
		if not dictionary.has(key):
			printerr("FAIL: snapshot has no key '%s'" % key)
			return false
	return true


func _sequence_of(sheet) -> Array:
	var snapshot: Dictionary = sheet.get_technique_snapshot()
	return snapshot.get("sequence", [])


func _sequence_ids(sheet) -> Array:
	var ids := []
	for entry in _sequence_of(sheet):
		ids.append(int(entry["technique_id"]))
	return ids


func _describe(snapshot: Dictionary) -> String:
	# Enough of the state to say "nothing moved" when a rejected command moved something.
	return "%s|%s|%s|%s|%s" % [str(snapshot.get("enabled")), str(snapshot.get("player_controlled")),
		str(snapshot.get("index")), str(snapshot.get("sequence")), str(snapshot.get("cooldowns"))]


# --- the methods exist, and every read answers a usable shape -----------------

func _surface_and_schema() -> void:
	var sheet = _bare_sheet()
	for method in ["get_equipment_snapshot", "get_weapon_snapshot", "equip_item", "unequip_item",
			"get_technique_snapshot", "configure_techniques"]:
		_check(sheet.has_method(method), "OpenCharacter exposes %s" % method)
	if not sheet.has_method("get_technique_snapshot"):
		return

	# An unconfigured sheet is the worst case for every read: no crash, no null, and the
	# same keys a configured sheet answers with.
	var empty_rows: Array = sheet.get_equipment_snapshot()
	_check(empty_rows is Array and empty_rows.is_empty(),
			"an unconfigured sheet owns no equipment rows")
	var empty_weapon: Dictionary = sheet.get_weapon_snapshot(1)
	_check(empty_weapon is Dictionary and empty_weapon.is_empty(),
			"and resolves no weapon snapshot")
	var empty_techniques: Dictionary = sheet.get_technique_snapshot()
	_check(_has_schema(empty_techniques, ["enabled", "player_controlled", "index", "sequence",
			"supported", "cooldowns", "can_configure"]),
			"the technique snapshot answers the whole schema even with no sheet")
	_check(empty_techniques.get("supported") is Array
			and (empty_techniques.get("supported") as Array).is_empty(),
			"with nothing resolved, nothing is supported")
	_check(empty_techniques.get("cooldowns") is Array
			and (empty_techniques.get("cooldowns") as Array).is_empty(),
			"and no cooldown is owed")
	_check(bool(empty_techniques.get("enabled")), "a sheet defaults to techniques enabled")
	_check(not bool(empty_techniques.get("player_controlled")),
			"and to the simulation choosing, not the player")
	_check(int(empty_techniques.get("index")) == 0, "with the sequence at its beginning")
	_check(not bool(empty_techniques.get("can_configure")),
			"and an unconfigured sheet cannot be configured")

	# Configured, with no items and no commands yet.
	var fresh = _sheet(1)
	var rows: Array = fresh.get_equipment_snapshot()
	_check(rows is Array and rows.is_empty(), "a sheet that owns nothing lists nothing")
	var bare: Dictionary = fresh.get_technique_snapshot()
	_check(_has_schema(bare, ["enabled", "player_controlled", "index", "sequence", "supported",
			"cooldowns", "can_configure"]), "the configured sheet answers the same schema")
	_check(bool(bare.get("can_configure")), "and a living sheet can be configured")
	_check((bare.get("sequence") as Array).is_empty(), "with an empty sequence")
	_check((bare.get("supported") as Array).is_empty(),
			"and nothing supported: bare hands are not a weapon with a technique list")
	for bad in [0, -1, 999999]:
		var missing: Dictionary = fresh.get_weapon_snapshot(bad)
		_check(missing is Dictionary and missing.is_empty(),
				"an item the sheet does not own resolves no weapon snapshot (id %s)" % bad)


# --- commands: what is refused, and what a refusal must not have touched -----

func _configure_rejections() -> void:
	var sheet = _sheet(2)
	var good = [{"technique_id": 7, "skip_on_cooldown": true}]
	_check(sheet.configure_techniques(true, good), "a well-formed step is accepted")
	var before := _describe(sheet.get_technique_snapshot())

	# Every malformed step shape is refused, and refused without touching the state that
	# was there: a popup that sends something silly cannot quietly cost the player a turn.
	var rejected := {
		"a step that is not a dictionary": [3],
		"a step with no keys": [{}],
		"a step with an unknown key": [{"technique_id": 3, "skip_on_cooldown": false,
				"extra": 1}],
		"a step missing skip_on_cooldown": [{"technique_id": 3}],
		"a step missing technique_id": [{"skip_on_cooldown": false}],
		"a technique id that is a string": [_step("3", false)],
		"a technique id that is a float": [_step(3.0, false)],
		"a technique id that is a non-integral float": [_step(3.5, false)],
		"a technique id that is null": [_step(null, false)],
		"a technique id of zero": [_step(0, false)],
		"a negative technique id": [_step(-3, false)],
		"a skip flag that is an int": [_step(3, 1)],
		"a skip flag that is a string": [_step(3, "false")],
		"a skip flag that is null": [_step(3, null)],
		"one malformed step among good ones": [_step(3, false), _step(0, false)],
	}
	for claim in rejected:
		_check(not sheet.configure_techniques(true, rejected[claim]), claim)
		_check(_describe(sheet.get_technique_snapshot()) == before,
				"%s leaves the saved sequence alone" % claim)

	# An unknown technique id is not an error: it is a thing the player configured and the
	# held weapon does not offer today. It is accepted and retained.
	_check(sheet.configure_techniques(true, [_step(UNKNOWN_TECHNIQUE, false)]),
			"an unknown technique id is accepted rather than rejected")
	_check(_sequence_ids(sheet) == [UNKNOWN_TECHNIQUE], "and it is retained")

	# An empty sequence is a real answer, not a missing one.
	_check(sheet.configure_techniques(true, []), "an empty sequence is accepted")
	_check(_sequence_of(sheet).is_empty(), "and it clears the sequence")
	_check(sheet.configure_techniques(false, []), "the switch is off")
	_check(not bool(sheet.get_technique_snapshot().get("enabled")),
			"and the snapshot says so")
	_check(sheet.configure_techniques(true, []), "and back on again")
	_check(bool(sheet.get_technique_snapshot().get("enabled")), "with the snapshot saying that")


func _configure_validation_edges() -> void:
	# Duplicates are separate positions in a sequence, not one entry visited twice, and the
	# bridge is where a list of them either survives or does not.
	var sheet = _sheet(3)
	var duplicated = [_step(4, false), _step(4, false), _step(5, true)]
	_check(sheet.configure_techniques(true, duplicated), "a sequence may repeat a technique")
	_check(_sequence_ids(sheet) == [4, 4, 5], "each copy is kept as its own entry")
	var stored: Array = _sequence_of(sheet)
	_check(bool(stored[2].get("skip_on_cooldown")), "with its own Skip setting")
	_check(not bool(stored[0].get("skip_on_cooldown")), "and its neighbour's Wait setting")

	# Every edit restarts the sequence at its first entry and nothing else; there is no way
	# for a script to set a clock or a position, so `index` is always the one the C++ state
	# holds rather than something the caller claims.
	_check(sheet.configure_techniques(true, [_step(4, false)]), "a shorter sequence is accepted")
	var replaced: Array = _sequence_of(sheet)
	_check(replaced.size() == 1 and int(replaced[0]["technique_id"]) == 4,
			"and it replaces the sequence rather than appending to it")
	_check(int(sheet.get_technique_snapshot().get("index")) == 0,
			"with the sequence back at its beginning")

	# The returned snapshot is a copy: poking at it cannot rewrite the sheet.
	var mutable: Dictionary = sheet.get_technique_snapshot()
	(mutable.get("sequence") as Array).clear()
	mutable["enabled"] = false
	mutable["index"] = 99
	var reread: Dictionary = sheet.get_technique_snapshot()
	_check(bool(reread.get("enabled")) and int(reread.get("index")) == 0
			and (reread.get("sequence") as Array).size() == 1,
			"mutating a snapshot changes the dictionary and not the sheet")
	var rows: Array = sheet.get_equipment_snapshot()
	rows.append({"item_id": 1})
	_check(sheet.get_equipment_snapshot().is_empty(),
			"and the same for the equipment rows")


func _equip_commands_are_refused_safely() -> void:
	var sheet = _sheet(4)
	for bad_id in [0, -1, -9223372036854775807]:
		_check(not sheet.equip_item(bad_id, SLOT_RIGHT_HAND),
				"an item id of %s is refused" % bad_id)
	for bad_slot in [-1, SLOT_COUNT, 999, -9223372036854775807]:
		_check(not sheet.equip_item(1, bad_slot), "slot %s is not a slot" % bad_slot)
	_check(not sheet.equip_item(1, SLOT_RIGHT_HAND),
			"and an item the sheet does not own cannot be equipped")
	_check(not sheet.unequip_item(SLOT_RIGHT_HAND), "nor unequipped from an empty slot")
	for bad_slot in [-1, SLOT_COUNT, 999]:
		_check(not sheet.unequip_item(bad_slot), "and unequipping slot %s is refused" % bad_slot)

	var unconfigured = _bare_sheet()
	_check(not unconfigured.equip_item(1, SLOT_RIGHT_HAND),
			"an unconfigured sheet equips nothing")
	_check(not unconfigured.unequip_item(SLOT_RIGHT_HAND), "and unequips nothing")
	_check(not unconfigured.configure_techniques(true, [_step(1, false)]),
			"and configures nothing")

	# A dead sheet has no equipment screen and no next attack to configure.
	var fallen = _sheet(5)
	_check(fallen.apply_damage(1, 1.0e30), "the sheet dies")
	_check(not fallen.is_alive(), "and C++ agrees")
	_check(not bool(fallen.get_technique_snapshot().get("can_configure")),
			"its snapshot says it cannot be configured")
	_check(not fallen.configure_techniques(true, [_step(1, false)]),
			"and a dead sheet's sequence cannot be edited")
	_check(not fallen.equip_item(1, SLOT_RIGHT_HAND), "nor equipped")
	_check(fallen.get_equipment_snapshot().is_empty(), "with nothing to list")


func _run() -> void:
	_surface_and_schema()
	_configure_rejections()
	_configure_validation_edges()
	_equip_commands_are_refused_safely()
	if failures == 0:
		print("Weapon bridge: all checks passed")
	quit(1 if failures else 0)
