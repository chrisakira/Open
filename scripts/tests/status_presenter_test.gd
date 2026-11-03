extends SceneTree

# Task 1 of the status UID plan: the snapshot formatter.
#
# `status_presenter.gd` is the one place the status views read a sheet, and it owns exactly two
# thoughts: what a sheet currently says (a fresh, disposable dictionary mirroring the bridge
# getters) and how a pool prints (whole numbers, ties half-up). It is
# never authority — this suite pins that snapshots mirror getters, that writing into a snapshot
# cannot write into the sheet, and that death is the sheet's own `is_alive()`, never an
# inference from the HP number.
#
# Like the action chat suite, the implementation is reached by guarded dynamic loading: a
# missing script or method is a counted failure here, not a parse cascade that ends the run
# without counting anything.

var failures := 0
var checks := 0

const PRESENTER_PATH := "res://scripts/status_presenter.gd"

const SNAPSHOT_KEYS := ["character_id", "character_name", "level", "faction", "alive",
		"hp", "stamina", "mana"]
const POOL_KEYS := ["current", "maximum"]


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


func _presenter():
	var script: Script = load(PRESENTER_PATH)
	if script == null:
		_check(false, "the presenter exists at %s" % PRESENTER_PATH)
	return script


# --- fixtures ------------------------------------------------------------------

func _player(id: int, character_name: String, level: int) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(id, character_name, 0, level, 0),
			"fixture player %d configures" % id)
	return sheet


func _npc(id: int, character_name: String, faction: int, level: int) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure_npc(id, character_name, faction, level, 1234 + id),
			"fixture NPC %d configures" % id)
	return sheet


func _pool(presenter, sheet: OpenCharacter, key: String, getter: String, max_getter: String) -> void:
	var pool: Dictionary = presenter.read_sheet(sheet)[key]
	_check(pool is Dictionary and pool.has_all(POOL_KEYS) and pool.size() == 2,
			"%s is a two-key pool snapshot" % key)
	_check(_close(float(pool["current"]), float(sheet.call(getter))),
			"the %s current mirrors the getter" % key)
	_check(_close(float(pool["maximum"]), float(sheet.call(max_getter))),
			"the %s maximum mirrors the getter" % key)


# --- cases ---------------------------------------------------------------------

func _a_configured_sheet_reads_into_an_honest_snapshot() -> void:
	var presenter = _presenter()
	if presenter == null:
		return
	var alpha := _player(11, "Test Alpha", 1)
	var beta := _npc(12, "Test Beta", 1, 7)
	for sheet in [alpha, beta]:
		var snap: Dictionary = presenter.read_sheet(sheet)
		_check(snap.has_all(SNAPSHOT_KEYS) and snap.size() == SNAPSHOT_KEYS.size(),
				"a snapshot carries exactly the identity and the three pools")
		_check(int(snap["character_id"]) == int(sheet.get_character_id()),
				"the id mirrors the getter")
		_check(String(snap["character_name"]) == String(sheet.get_character_name()),
				"the name mirrors the getter")
		_check(int(snap["level"]) == int(sheet.get_level()), "the level mirrors the getter")
		_check(int(snap["faction"]) == int(sheet.get_faction()),
				"the faction mirrors the getter")
		_check(bool(snap["alive"]) == bool(sheet.is_alive()),
				"alive is the sheet's own answer")
		_pool(presenter, sheet, "hp", "get_health", "get_max_health")
		_pool(presenter, sheet, "stamina", "get_stamina", "get_max_stamina")
		_pool(presenter, sheet, "mana", "get_mana", "get_max_mana")
	# Separate instances, separate identities: a snapshot can be a shared dict only by bug.
	var first: Dictionary = presenter.read_sheet(alpha)
	var second: Dictionary = presenter.read_sheet(beta)
	_check(int(first["character_id"]) == 11 and int(second["character_id"]) == 12,
			"ids stay independent between instances")
	_check(String(first["character_name"]) == "Test Alpha"
			and String(second["character_name"]) == "Test Beta",
			"names stay independent between instances")
	_check(int(first["faction"]) == 0 and int(second["faction"]) == 1,
			"factions stay independent between instances")
	_check(int(first["level"]) == 1 and int(second["level"]) == 7,
			"levels stay independent, covering both ends of the range")
	_check(int(presenter.read_sheet(alpha)["level"]) == 1,
			"a level-one sheet reports level one")
	_check(int(presenter.read_sheet(beta)["level"]) == 7,
			"a level-seven sheet reports level seven")


func _fractions_follow_their_own_maximum() -> void:
	var presenter = _presenter()
	if presenter == null:
		return
	_check(presenter.pool_fraction(50.0, 100.0) == 0.5, "50 of 100 fills half")
	_check(presenter.pool_fraction(25.0, 200.0) == 0.125, "25 of 200 fills an eighth")
	_check(presenter.pool_fraction(30.0, 60.0) == 0.5, "30 of 60 fills half, not a fixed 100")
	_check(presenter.pool_fraction(7.5, 24.0) == 7.5 / 24.0, "a fractional maximum divides itself")
	_check(presenter.pool_fraction(0.0, 100.0) == 0.0, "zero current is an empty bar")
	_check(presenter.pool_fraction(100.0, 100.0) == 1.0, "full is a full bar")
	var tiny = presenter.pool_fraction(0.001, 100.0)
	_check(tiny > 0.0 and tiny == 0.001 / 100.0, "a tiny positive stays positive")


func _the_readout_prints_whole_numbers() -> void:
	var presenter = _presenter()
	if presenter == null:
		return
	_check(presenter.pool_text("HP", 99.96, 100.0) == "HP 100/100", "99.96 rounds up")
	_check(presenter.pool_text("Stamina", 85.0, 100.0) == "Stamina 85/100", "85 stays 85")
	_check(presenter.pool_text("Mana", 12.344, 100.0) == "Mana 12/100", "12.344 rounds down")
	_check(presenter.pool_text("Mana", 12.6, 100.0) == "Mana 13/100", "12.6 rounds up")
	_check(presenter.pool_text("HP", 0.001, 100.0) == "HP 0/100", "a tiny positive prints zero")
	_check(presenter.pool_text("Stamina", 0.5, 100.0) == "Stamina 1/100", "a tie rounds half-up")
	_check(presenter.pool_text("HP", 100.5, 101.5) == "HP 101/102", "maximum rounds half-up too")


func _readouts_use_whole_numbers() -> void:
	var presenter = _presenter()
	_check(presenter.pool_amounts(100.0, 100.0) == "100/100", "full pools use integer values")
	_check(presenter.pool_amounts(12.49, 99.5) == "12/100", "values round to nearest whole number")
	_check(presenter.pool_amounts(12.5, 100.0) == "13/100", "half values round up")


func _death_is_the_sheets_answer_never_the_number() -> void:
	var presenter = _presenter()
	if presenter == null:
		return
	var head := _player(21, "Headshot", 1)
	_check(head.apply_damage(0, 25.0), "the head takes its full health")
	var head_snap: Dictionary = presenter.read_sheet(head)
	_check(not bool(head_snap["alive"]), "a zero head kills, the bridge says so")
	_check(_close(float(head_snap["hp"]["current"]), 75.0),
			"and main health is still 75 of 100")
	_check(_close(float(head_snap["hp"]["maximum"]), 100.0), "the head's victim keeps its maximum")

	var torso := _player(22, "Torso", 1)
	_check(torso.apply_damage(1, 50.0), "the torso takes its full health")
	var torso_snap: Dictionary = presenter.read_sheet(torso)
	_check(not bool(torso_snap["alive"]), "a zero torso kills")
	_check(_close(float(torso_snap["hp"]["current"]), 50.0), "with half main health standing")

	var tiny := _player(23, "Almost", 1)
	_check(tiny.apply_damage(2, 40.0) and tiny.apply_damage(3, 40.0)
			and tiny.apply_damage(4, 19.999), "limb hits land")
	var tiny_snap: Dictionary = presenter.read_sheet(tiny)
	var h := float(tiny.get_health())
	_check(h > 0.0 and h < 0.005, "main health is tiny and positive, read as a real float")
	_check(bool(tiny_snap["alive"]), "and the sheet is alive because C++ says so")
	_check(presenter.pool_text("HP", h, float(tiny.get_max_health()))
			== "HP 0/100", "the text shows 0 while the living fraction stays positive")
	_check(presenter.pool_fraction(h, float(tiny.get_max_health())) > 0.0,
			"the fraction knows displayed zero is not necessarily empty")

	var drained := _player(24, "Drained", 1)
	_check(drained.apply_damage(4, 200.0), "one hit takes everything the leg had and the main pool too")
	var drained_snap: Dictionary = presenter.read_sheet(drained)
	_check(not bool(drained_snap["alive"]), "zero main health kills")
	_check(_close(float(drained_snap["hp"]["current"]), 0.0), "and the current is truly zero")

	# Reading is not doing: snapshots leave the sheet, its order and its clocks alone, and
	# writing into a snapshot cannot write back.
	var witness := _player(25, "Witness", 1)
	var before_health := float(witness.get_health())
	var before_target := int(witness.get_attack_target_id())
	var before_velocity := witness.velocity_at(Vector3.ZERO)
	var snap: Dictionary = presenter.read_sheet(witness)
	snap["character_name"] = "Hacked"
	snap["hp"]["current"] = 0.0
	snap["alive"] = false
	_check(String(witness.get_character_name()) == "Witness", "a snapshot cannot rename a sheet")
	_check(_close(float(witness.get_health()), before_health), "a snapshot cannot wound a sheet")
	_check(bool(witness.is_alive()), "a snapshot cannot kill a sheet")
	_check(int(witness.get_attack_target_id()) == before_target, "reading touches no order")
	_check(witness.velocity_at(Vector3.ZERO) == before_velocity, "reading starts no motion")


func _a_new_maximum_shows_in_a_new_snapshot() -> void:
	var presenter = _presenter()
	if presenter == null:
		return
	var sheet := _player(31, "Rising", 1)
	var before: Dictionary = presenter.read_sheet(sheet)
	_check(bool(sheet.power_up()), "the playtest lever powers the sheet up")
	var after: Dictionary = presenter.read_sheet(sheet)
	for pair in [["hp", "get_health", "get_max_health"],
			["stamina", "get_stamina", "get_max_stamina"],
			["mana", "get_mana", "get_max_mana"]]:
		_check(_close(float(after[pair[0]]["current"]), float(sheet.call(pair[1]))),
				"after a maximum change the %s current still mirrors the getter" % pair[0])
		_check(_close(float(after[pair[0]]["maximum"]), float(sheet.call(pair[2]))),
				"after a maximum change the %s maximum mirrors the getter too" % pair[0])
	_check(float(after["hp"]["maximum"]) > float(before["hp"]["maximum"]),
			"the maximum really moved — the snapshot follows instead of assuming")


# --- runner --------------------------------------------------------------------

func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	_case("a configured sheet reads into an honest snapshot", 36,
			_a_configured_sheet_reads_into_an_honest_snapshot)
	_case("fractions follow their own maximum", 7, _fractions_follow_their_own_maximum)
	_case("the readout prints whole numbers", 7, _the_readout_prints_whole_numbers)
	_case("readouts use whole numbers", 3, _readouts_use_whole_numbers)
	_case("death is the sheet's answer, never the number", 18,
			_death_is_the_sheets_answer_never_the_number)
	_case("a new maximum shows in a new snapshot", 9, _a_new_maximum_shows_in_a_new_snapshot)

	_check(checks > 85, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Status presenter: all checks passed")
	quit(1 if failures else 0)
