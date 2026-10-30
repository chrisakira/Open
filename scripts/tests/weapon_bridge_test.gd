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


# --- Task 14b: the same bridge over the real starter content -------------------
#
# Everything above pins the contract against a sheet that owns nothing, because in 14a nothing
# could. Here the items and techniques are the production ones: the coordinator's own setup
# command mints the backpack, the catalog resolves the real definitions, and a fight run through
# this very bridge starts real clocks. No fixture technique ids below - 1/2/3 are the starter
# moves, and every id comes from a snapshot, never from a hardcoded instance number.

const SLASH := 1
const THRUST := 2
const BLUNT_EDGE := 3

# Which starter techniques each starter weapon offers, from docs/weapons.md's table.
const OFFERED_BY_MODEL := {
	"Sword": [SLASH, THRUST],
	"Dagger": [SLASH, THRUST],
	"Spear": [THRUST],
	"Hammer_Small": [BLUNT_EDGE],
}


func _step_ids(steps: Array) -> Array:
	var ids := []
	for step in steps:
		ids.append(int(step["technique_id"]))
	return ids


func _supported_ids(sheet) -> Array:
	var ids := []
	for row in sheet.get_technique_snapshot().get("supported", []):
		ids.append(int(row["technique_id"]))
	return ids


func _cooldown_of(sheet, technique_id: int) -> float:
	for row in sheet.get_technique_snapshot().get("cooldowns", []):
		if int(row["technique_id"]) == technique_id:
			return float(row["cooldown_remaining"])
	return 0.0


func _model_ids(sheet) -> Dictionary:
	var by_model := {}
	for row in sheet.get_equipment_snapshot():
		by_model[String(row["model_key"])] = int(row["item_id"])
	return by_model


func _real_backpack(actor_id: int) -> Dictionary:
	# A real coordinator and a real sheet: the same two classes the scene builds.
	var sheet = _sheet(actor_id)
	var combat = ClassDB.instantiate(&"OpenCombat")
	_check(combat.add_character(sheet, Vector3.ZERO), "the fixture sheet joins the coordinator")
	_check(combat.setup_starter_player(actor_id), "and the production command gives it the backpack")
	_check(combat.setup_starter_player(actor_id), "and is legal to call again")
	var by_model := _model_ids(sheet)
	_check(by_model.size() == 5 and sheet.get_equipment_snapshot().size() == 5,
			"still exactly one of each: setup through the bridge cannot mint twice")
	return {"sheet": sheet, "combat": combat, "ids": by_model}


func _real_rows_round_trip() -> void:
	var kit := _real_backpack(10)
	var sheet = kit["sheet"]
	var by_model: Dictionary = kit["ids"]
	var definitions := []
	for row in sheet.get_equipment_snapshot():
		definitions.append(int(row["definition_id"]))
	definitions.sort()
	_check(definitions == [101, 102, 103, 104, 105],
			"the owned instances resolve the production definitions: %s" % str(definitions))

	# A real snapshot row is a copy: scribbling on what a read returned cannot rewrite the sheet.
	var first: Dictionary = sheet.get_equipment_snapshot()[0]
	var first_name := String(first["model_key"])
	first["model_key"] = "Rewritten"
	first["slots"] = [7, 8, 9]
	first["durability"] = -5
	var reread: Dictionary = sheet.get_equipment_snapshot()[0]
	_check(String(reread["model_key"]) == first_name and int(reread["durability"]) == 100
			and (reread["slots"] as Array).is_empty(),
			"mutating a real equipment row changes the dictionary and not the sheet")

	# Variant edges against ids that are genuinely alive on some other sheet. The second sheet
	# joins the same coordinator, so both backpacks come from the one allocator and the ids
	# really are somebody else's.
	var other = _sheet(11)
	_check(kit["combat"].add_character(other, Vector3(50.0, 0.0, 50.0)),
			"a second sheet joins the same coordinator")
	_check(kit["combat"].setup_starter_player(11), "and is given its own kit by the same command")
	var foreign_sword := int(_model_ids(other)["Sword"])
	_check(foreign_sword != 0 and foreign_sword != int(by_model["Sword"]),
			"the second sheet's sword is a real instance with its own id")
	var before := _held_description(sheet)
	_check(not sheet.equip_item(foreign_sword, SLOT_RIGHT_HAND),
			"a real item another sheet owns is refused here")
	_check(not sheet.equip_item(987654321, SLOT_RIGHT_HAND), "an unknown positive id is refused")
	_check(_held_description(sheet) == before, "and neither refusal touched what is held")
	_check(sheet.get_weapon_snapshot(foreign_sword).is_empty(),
			"and resolves no weapon snapshot for it either")
	# The shield is owned but not a weapon: it resolves no weapon snapshot.
	_check(sheet.get_weapon_snapshot(int(by_model["Shield_Round"])).is_empty(),
			"a shield offers no attack profile")


func _held_description(sheet) -> String:
	var parts := []
	for row in sheet.get_equipment_snapshot():
		parts.append("%s:%s" % [String(row["model_key"]), str(row["slots"])])
	parts.sort()
	return "|".join(parts)


func _unhold_all(sheet) -> void:
	# Free both hands however the last item was worn, through the bridge's own answers.
	for pass_no in range(3):
		var released := false
		for row in sheet.get_equipment_snapshot():
			var slots: Array = row["slots"]
			if not slots.is_empty():
				_check(sheet.unequip_item(int(slots[0])),
						"the %s comes out of the hand(s)" % String(row["model_key"]))
				released = true
		if not released:
			return


func _real_weapons_offer_their_real_techniques() -> void:
	var kit := _real_backpack(12)
	var sheet = kit["sheet"]
	var by_model: Dictionary = kit["ids"]
	_check(_supported_ids(sheet).is_empty(), "bare hands offer no technique through the bridge")

	# A configured sequence survives every swap below: support comes and goes with the held
	# weapon, the player's list does not.
	_check(sheet.configure_techniques(true, [_step(SLASH, true), _step(UNKNOWN_TECHNIQUE, false)]),
			"the sheet takes a sequence with a foreign entry")
	var sequence_before := _sequence_ids(sheet)

	for model in ["Sword", "Dagger", "Spear", "Hammer_Small"]:
		_unhold_all(sheet)
		var item := int(by_model[String(model)])
		_check(sheet.equip_item(item, SLOT_RIGHT_HAND), "the %s is held" % String(model))
		_check(_supported_ids(sheet) == OFFERED_BY_MODEL[String(model)],
				"%s supports exactly %s through the bridge, got %s"
				% [String(model), str(OFFERED_BY_MODEL[String(model)]), str(_supported_ids(sheet))])
		var weapon: Dictionary = sheet.get_weapon_snapshot(item)
		var weapon_techniques := []
		for technique_id in weapon.get("techniques", []):
			weapon_techniques.append(int(technique_id))
		_check(weapon_techniques == OFFERED_BY_MODEL[String(model)],
				"and the weapon snapshot names the same list")
		for row in sheet.get_technique_snapshot().get("supported", []):
			_check(bool(row["defined"]) and bool(row["available"]),
					"a starter technique on a level-1 sheet is defined and unlocked, not a fixture")
		_check(_sequence_ids(sheet) == sequence_before,
				"and swapping to the %s kept the configured sequence" % String(model))

	# The numbers on the rows are the locked table's.
	_unhold_all(sheet)
	_check(sheet.equip_item(int(by_model["Sword"]), SLOT_RIGHT_HAND), "the sword is back in hand")
	for row in sheet.get_technique_snapshot().get("supported", []):
		if int(row["technique_id"]) == SLASH:
			_check(_close(float(row["cooldown"]), 6.0)
					and _close(float(row["cut"]), 1.25)
					and _close(float(row["pierce"]), 1.0)
					and not bool(row["has_stamina_cost"]),
					"Slash resolves its locked 6s cooldown, cut factor and no cost override")
		_check(_close(float(row["required_rank"]), 1.0), "and every starter move asks rank 1")


func _controller_handover_round_trips() -> void:
	var sheet = _sheet(13)
	_check(sheet.has_method("set_player_controlled"),
			"the bridge exposes the controller handover C++ already has")
	if not sheet.has_method("set_player_controlled"):
		return
	_check(not bool(sheet.get_technique_snapshot().get("player_controlled")),
			"a fresh sheet is simulation-driven")
	_check(sheet.set_player_controlled(true), "the handover succeeds on a living sheet")
	_check(bool(sheet.get_technique_snapshot().get("player_controlled")),
			"and the snapshot says the player chooses now")

	# Only who chooses changes: an edit-free toggle keeps the sequence, the switch and the index.
	_check(sheet.configure_techniques(false, [_step(SLASH, true)]), "the sheet holds one off entry")
	_check(sheet.set_player_controlled(false), "and can be handed back")
	var snapshot: Dictionary = sheet.get_technique_snapshot()
	_check(not bool(snapshot.get("enabled")) and _sequence_ids(sheet) == [SLASH]
			and int(snapshot.get("index")) == 0,
			"while switch, sequence and position stay exactly where the player left them")

	var unconfigured = _bare_sheet()
	_check(not unconfigured.set_player_controlled(true), "an unconfigured sheet is not a handover")
	var fallen = _sheet(14)
	_check(fallen.apply_damage(1, 1.0e30) and not fallen.is_alive(), "and a dead sheet")
	_check(not fallen.set_player_controlled(true), "gets no handover either")


func _a_performed_technique_starts_a_real_clock() -> void:
	var kit := _real_backpack(15)
	var sheet = kit["sheet"]
	var combat = kit["combat"]
	if not sheet.has_method("set_player_controlled"):
		_check(false, "the fight fixture needs the controller handover")
		return
	var victim = _sheet(16)
	var bystander = _sheet(17)
	combat.add_character(victim, Vector3(1.0, 0.0, 0.0))
	combat.add_character(bystander, Vector3(20.0, 0.0, 20.0))

	_check(sheet.equip_item(int(_model_ids(sheet)["Sword"]), SLOT_RIGHT_HAND),
			"the attacker fights with the sword it owns")
	_check(sheet.set_player_controlled(true), "and chooses its own moves")
	_check(sheet.configure_techniques(true, [_step(SLASH, true), _step(BLUNT_EDGE, true)]),
			"the sequence is Slash, then the hammer move a sword does not offer")
	_check(combat.issue_attack(15, 16), "an order is on")

	var events: Array = []
	var guard := 0
	while guard < 60 and events.is_empty():
		combat.step(0.1)
		events = combat.take_combat_events()
		guard += 1
	_check(events.size() == 1, "one performed swing is reported, got %d" % events.size())
	if events.is_empty():
		return
	_check(int(events[0]["technique_id"]) == SLASH,
			"and it performed the sequence's first entry, not the unsupported one")
	_check(_cooldown_of(sheet, BLUNT_EDGE) == 0.0,
			"an entry skipped for being unsupported starts no clock")

	var cooling := _cooldown_of(sheet, SLASH)
	_check(cooling > 5.0, "the performed Slash is cooling from its own 6s: %s" % str(cooling))
	_check(_cooldown_of(victim, SLASH) == 0.0, "the clock belongs to the one who swung")

	# Swapping weapons between two steps starts no clock and spends none: the value has to
	# carry across untouched, and the sequence with it.
	_check(sheet.unequip_item(SLOT_RIGHT_HAND), "the sword comes off")
	_check(sheet.equip_item(int(_model_ids(sheet)["Dagger"]), SLOT_RIGHT_HAND),
			"the dagger goes on")
	_check(_close(_cooldown_of(sheet, SLASH), cooling),
			"the swap across supporting weapons changed no clock")
	_check(_sequence_ids(sheet) == [SLASH, BLUNT_EDGE], "and erased no entry")

	# The next swing, still inside the cooldown: Slash is Skip-while-cooling, the hammer move
	# is unsupported on this weapon too, one loop finds nothing, and the attack is a basic one.
	events = []
	guard = 0
	while guard < 200 and events.is_empty():
		combat.step(0.1)
		events = combat.take_combat_events()
		guard += 1
	_check(events.size() == 1, "the fight swung again on its own clock")
	if events.size() == 1:
		_check(int(events[0]["technique_id"]) == 0,
				"with every entry skipped, the performed attack is the weapon's own basic one")
	_check(_cooldown_of(sheet, SLASH) < cooling,
			"while the clock kept wearing down between the swings, once per step")
	_check(_cooldown_of(sheet, SLASH) > 0.0, "and was still running at the second swing")


func _run() -> void:
	_surface_and_schema()
	_configure_rejections()
	_configure_validation_edges()
	_equip_commands_are_refused_safely()
	_real_rows_round_trip()
	_real_weapons_offer_their_real_techniques()
	_controller_handover_round_trips()
	_a_performed_technique_starts_a_real_clock()
	if failures == 0:
		print("Weapon bridge: all checks passed")
	quit(1 if failures else 0)
