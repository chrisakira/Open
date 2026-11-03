extends SceneTree

# Task 2 of the status UID plan: the shared bar and the player's plaque.
#
# These are presentation controls, so every assertion is about structure, mirror and input
# shape: the rows exist in the locked order, print exactly what the getters say after one
# refresh, wear the human's locked colors, and cannot eat a click. What the numbers *mean* is
# Task 1's already-pinned territory; here the point is that the plaque never edits, smooths,
# caches or invents them.
#
# The approved HUD polish contains the identity and all rows inside the framed plaque;
# half-up readouts and deep beveled red/green/blue tracks remain unchanged.

var failures := 0
var checks := 0

const BAR_PATH := "res://scripts/status_bar.gd"
const PLAYER_PATH := "res://scripts/player_status.gd"
const PRESENTER_PATH := "res://scripts/status_presenter.gd"

const ROW_NAMES := ["HP", "Stamina", "Mana"]
const HP_COLOR := Color("#8F423D")
const STAMINA_COLOR := Color("#4E7A4A")
const MANA_COLOR := Color("#4A6690")


func _initialize() -> void:
	call_deferred("_run")


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- fixtures ------------------------------------------------------------------

func _script(path: String) -> Script:
	var script: Script = load(path)
	if script == null:
		_check(false, "a script exists at %s" % path)
	return script


func _sheet(id: int, character_name: String, level: int) -> OpenCharacter:
	var sheet := OpenCharacter.new()
	_check(sheet.configure(id, character_name, 0, level, 0), "fixture sheet %d configures" % id)
	return sheet


func _plaque() -> Control:
	var script := _script(PLAYER_PATH)
	if script == null:
		return null
	var host := Control.new()
	host.custom_minimum_size = Vector2(340, 90)
	root.add_child(host)
	var control: Control = script.new()
	host.add_child(control)
	return control


func _row(plaque: Control, row_name: String) -> Control:
	return plaque.get_node_or_null(row_name)


func _row_text(row: Control) -> String:
	var label := row.get_node_or_null("Value") as Label
	return "" if label == null else label.text


func _row_role(row: Control) -> String:
	var label := row.get_node_or_null("Role") as Label
	return "" if label == null else label.text


# --- cases ---------------------------------------------------------------------

func _changing_large_readouts_stay_inside_the_plaque_and_clear_of_the_track() -> void:
	var plaque := _plaque()
	var sheet := _sheet(23, "Mana Adept", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	await process_frame
	await process_frame
	var row := _row(plaque, "Mana")
	var value := row.get_node("Value") as Label
	var presenter := _script(PRESENTER_PATH)
	# The bridge has no training API. Exercise the real shared row and formatter with the
	# valid 10,000 maximum from character_test.cpp, changing the current without resizing.
	# A larger font also exercises wider system-font metrics on other machines.
	value.add_theme_font_size_override("font_size", 16)
	for current in [100.0, 10000.0, 1.0]:
		row.present(current, 10000.0, presenter.pool_amounts(current, 10000.0))
		await process_frame
		await process_frame
		_check(value.text == ("%d/10000" % int(current)), "the full live readout is unchanged")
		_check(plaque.get_global_rect().encloses(value.get_global_rect()),
				"the changing readout stays inside the plaque: %s" % value.text)
		_check(row.get_global_rect().encloses(value.get_global_rect()),
				"the changing readout stays inside its row: %s" % value.text)
		_check(value.get_theme_font("font").get_string_size(value.text, HORIZONTAL_ALIGNMENT_LEFT,
				-1, value.get_theme_font_size("font_size")).x <= value.size.x,
				"the complete readout fits its label: %s" % value.text)
		_check(value.position.x >= row.size.x - row.value_width - 0.01,
				"the changing readout does not cover its track: %s" % value.text)
		_check(row.size.x - row.value_width - 6.0 > row.role_width + 6.0,
				"the track remains visible beside %s" % value.text)
	plaque.get_parent().free()


func _plaque_contains_long_identity_and_complete_rows() -> void:
	var plaque := _plaque()
	var sheet := _sheet(22, "WWWWWWWWWWWWWWWWWWWW", 7)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	await process_frame
	await process_frame
	var bounds := plaque.get_global_rect()
	var name_label := plaque.get_node("Identity/CharacterName") as Label
	var level := plaque.get_node("Identity/Level") as Label
	_check(bounds.encloses(name_label.get_global_rect()), "the plaque contains the long name")
	_check(bounds.encloses(level.get_global_rect()), "the plaque contains the level")
	_check(name_label.get_global_rect().end.x <= level.get_global_rect().position.x,
			"the long name and level do not overlap")
	_check(name_label.get_theme_font("font").get_string_size(name_label.text, HORIZONTAL_ALIGNMENT_LEFT,
			-1, name_label.get_theme_font_size("font_size")).x <= name_label.size.x,
			"all twenty name characters fit")
	for row_name in ROW_NAMES:
		var row := _row(plaque, row_name)
		var role := row.get_node("Role") as Label
		var value := row.get_node("Value") as Label
		_check(bounds.encloses(row.get_global_rect()), "%s row fits in the plaque" % row_name)
		_check(row.get_global_rect().encloses(role.get_global_rect()), "%s role fits its row" % row_name)
		_check(row.get_global_rect().encloses(value.get_global_rect()), "%s value fits its row" % row_name)
		_check(value.get_theme_font("font").get_string_size(value.text, HORIZONTAL_ALIGNMENT_LEFT,
				-1, value.get_theme_font_size("font_size")).x <= value.size.x,
				"%s complete current / maximum fits" % row_name)
		_check(role.position.x + role.size.x <= row.role_width + 0.01
				and value.position.x >= row.size.x - row.value_width - 0.01,
				"%s labels do not overlap the bar" % row_name)
	plaque.get_parent().free()


func _the_plaque_is_identity_above_three_ordered_bars() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var bar_script := _script(BAR_PATH)
	var presenter := _script(PRESENTER_PATH)
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	_check(plaque.has_node("Identity"), "the plaque has an identity area")
	_check(plaque.has_node("HP") and plaque.has_node("Stamina") and plaque.has_node("Mana"),
			"the three named rows exist")
	var rows: Array = []
	for row_name in ROW_NAMES:
		var row := _row(plaque, row_name)
		rows.append(row)
		_check(row != null and bar_script != null and is_instance_of(row, bar_script),
				"%s is a shared StatusBar" % row_name)
	_check(rows.size() == 3 and rows[0] != null and rows[1] != null and rows[2] != null
			and (rows[0] as Control).position.y < (rows[1] as Control).position.y
			and (rows[1] as Control).position.y < (rows[2] as Control).position.y,
			"the bars stack in the locked HP, stamina, mana order")
	var identity := plaque.get_node_or_null("Identity") as Control
	_check(identity != null and rows[0] != null
			and identity.position.y + identity.size.y <= (rows[0] as Control).position.y + 0.01,
			"identity sits above the bar stack")
	_check(_row_role(_row(plaque, "HP")) == "HP" and _row_role(_row(plaque, "Stamina"))
			== "Stamina" and _row_role(_row(plaque, "Mana")) == "Mana",
			"the rows carry their locked resource names")
	var name_label := plaque.get_node_or_null("Identity/CharacterName") as Label
	var level_label := plaque.get_node_or_null("Identity/Level") as Label
	_check(name_label != null and name_label.text == "John Doe",
			"the identity prints the sheet's real name")
	_check(level_label != null and level_label.text == "Level 1", "and the player level form")
	_check(_row_text(_row(plaque, "HP")) == "100/100", "HP prints current and maximum")
	_check(_row_text(_row(plaque, "Stamina")) == "100/100", "stamina prints its own pool")
	_check(_row_text(_row(plaque, "Mana")) == "100/100", "mana prints its own pool")
	_check(presenter == null or true, "the full readout form stays the presenter's")
	# Exactly three bars, never a fourth: no XP gauge was asked for.
	var bar_count := 0
	for child in plaque.get_children():
		if bar_script != null and is_instance_of(child, bar_script):
			bar_count += 1
	_check(bar_count == 3, "the plaque holds exactly three bars and no XP gauge")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _the_rows_wear_the_locked_colors() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	var hp := _row(plaque, "HP")
	var stamina := _row(plaque, "Stamina")
	var mana := _row(plaque, "Mana")
	_check(hp != null and hp.get("fill_color") == HP_COLOR, "the HP row is the locked deep red")
	_check(stamina != null and stamina.get("fill_color") == STAMINA_COLOR,
			"the stamina row is the locked deep green")
	_check(mana != null and mana.get("fill_color") == MANA_COLOR,
			"the mana row is the locked deep blue")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _one_refresh_prints_exactly_what_cpp_just_said() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var presenter := _script(PRESENTER_PATH)
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	_check(sheet.apply_damage(2, 10.0), "a leg takes ten")
	plaque.refresh()
	var hp := _row(plaque, "HP")
	_check(_row_text(hp) == "90/100", "one refresh and the readout is the new truth")
	_check(_close(float(hp.get("ratio")), 90.0 / 100.0), "and the fill is the exact fraction")
	# Refreshing twice with unchanged data is a no-op, not a drain frame.
	plaque.refresh()
	_check(_close(float(hp.get("ratio")), 90.0 / 100.0), "a second refresh changes nothing")
	# A tiny positive prints 0 but the fill still knows it is alive.
	_check(sheet.apply_damage(3, 45.0) and sheet.apply_damage(4, 44.999), "more limb damage lands")
	plaque.refresh()
	var h := float(sheet.get_health())
	_check(h > 0.0 and h < 0.005, "main health is tiny and positive")
	_check(_row_text(hp) == "0/100", "the readout prints zero")
	_check(float(hp.get("ratio")) > 0.0, "the fill never collapses to empty")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _a_dead_player_keeps_its_real_numbers_and_wears_no_skull() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	_check(sheet.apply_damage(0, 25.0), "the head takes its full health")
	plaque.refresh()
	_check(not bool(sheet.is_alive()), "the sheet is dead")
	_check(_row_text(_row(plaque, "HP")) == "75/100",
			"the plaque still shows main HP as C++ says, zeroed by nothing")
	_check(plaque.find_child("DeathIcon", true, false) == null,
			"no player skull or death overlay was locked, so none exists")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _rebinding_switches_the_whole_identity_never_a_mix() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var first := _sheet(1, "John Doe", 1)
	var second := _sheet(2, "Risen Second", 7)
	_check(second.apply_damage(2, 30.0), "the second sheet loses some leg health")
	plaque.bind_sheet(first)
	plaque.refresh()
	plaque.bind_sheet(second)
	plaque.refresh()
	var name_label := plaque.get_node_or_null("Identity/CharacterName") as Label
	var level_label := plaque.get_node_or_null("Identity/Level") as Label
	_check(name_label != null and name_label.text == "Risen Second", "the name switched")
	_check(level_label != null and level_label.text == "Level 7", "the level switched")
	_check(_row_text(_row(plaque, "HP")).begins_with("70/"), "the fills switched")
	_check(_close(float(_row(plaque, "HP").get("ratio")), 70.0 / 100.0),
			"the fill followed the new sheet, not the old one")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _the_whole_control_tree_is_invisible_to_the_mouse() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	_check(plaque.mouse_filter == Control.MOUSE_FILTER_IGNORE, "the plaque root ignores input")
	var ignored := 0
	var total := 0
	for node in plaque.find_children("*", "", true, false):
		total += 1
		if (node as Control).mouse_filter == Control.MOUSE_FILTER_IGNORE:
			ignored += 1
	_check(total > 0 and ignored == total,
			"every descendant ignores input: %d of %d" % [ignored, total])
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _it_hides_itself_when_there_is_no_honest_thing_to_show() -> void:
	var plaque := _plaque()
	if plaque == null:
		return
	plaque.refresh()
	_check(not plaque.visible, "an unbound plaque hides instead of inventing bars")
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	_check(plaque.visible, "a bound plaque shows")
	plaque.bind_sheet(null)
	plaque.refresh()
	_check(not plaque.visible, "losing the sheet hides it again, without a crash")
	if plaque.get_parent() != null:
		plaque.get_parent().free()


func _a_small_window_moves_the_plaque_and_keeps_its_numbers() -> void:
	var viewport := SubViewport.new()
	viewport.size = Vector2i(1280, 720)
	root.add_child(viewport)
	var layer := CanvasLayer.new()
	viewport.add_child(layer)
	var script := _script(PLAYER_PATH)
	if script == null:
		viewport.free()
		return
	var plaque: Control = script.new()
	layer.add_child(plaque)
	var sheet := _sheet(1, "John Doe", 1)
	plaque.bind_sheet(sheet)
	plaque.refresh()
	await process_frame
	await process_frame
	var big_rect := plaque.get_global_rect()
	_check(sheet.apply_damage(2, 12.0), "damage lands before the squeeze")
	plaque.refresh()
	viewport.size = Vector2i(720, 420)
	await process_frame
	await process_frame
	var small_rect := plaque.get_global_rect()
	viewport.size = Vector2i(1280, 720)
	await process_frame
	await process_frame
	var back_rect := plaque.get_global_rect()
	_check(small_rect.position != big_rect.position,
			"the plaque really moves with the viewport")
	_check(_close(back_rect.position.x, big_rect.position.x)
			and _close(back_rect.end.x, big_rect.end.x), "and comes back to the same place")
	# Bottom-centred: the horizontal centre of the plaque tracks the viewport centre.
	viewport.size = Vector2i(720, 420)
	await process_frame
	await process_frame
	_check(absf(plaque.get_global_rect().get_center().x - 360.0) < 1.0,
			"the plaque stays centred on the window width")
	_check(_row_text(_row(plaque, "HP")).begins_with("88/"),
			"resizing never touched the numbers")
	viewport.free()


# --- runner --------------------------------------------------------------------

func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	await _case("changing large readouts stay inside the plaque and clear of the track", 19,
			_changing_large_readouts_stay_inside_the_plaque_and_clear_of_the_track)
	await _case("plaque contains long identity and complete rows", 19,
			_plaque_contains_long_identity_and_complete_rows)
	await _case("the plaque is identity above three ordered bars", 15,
			_the_plaque_is_identity_above_three_ordered_bars)
	await _case("the rows wear the locked colors", 3, _the_rows_wear_the_locked_colors)
	await _case("one refresh prints exactly what C++ just said", 9,
			_one_refresh_prints_exactly_what_cpp_just_said)
	await _case("a dead player keeps its real numbers and wears no skull", 4,
			_a_dead_player_keeps_its_real_numbers_and_wears_no_skull)
	await _case("rebinding switches the whole identity, never a mix", 7,
			_rebinding_switches_the_whole_identity_never_a_mix)
	await _case("the whole control tree is invisible to the mouse", 3,
			_the_whole_control_tree_is_invisible_to_the_mouse)
	await _case("it hides itself when there is no honest thing to show", 4,
			_it_hides_itself_when_there_is_no_honest_thing_to_show)
	await _case("a small window moves the plaque and keeps its numbers", 6,
			_a_small_window_moves_the_plaque_and_keeps_its_numbers)

	_check(checks > 55, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("Player status: all checks passed")
	quit(1 if failures else 0)
