extends SceneTree

# Task 3 of the status UID plan: the world overhead.
#
# Every assertion here is the honest-mirror kind the plaque suite makes, aimed at other
# characters: the overhead reads the *actor's current sheet* (never a cached copy or the
# player's numbers), the relationship diamond is faction and nothing else, death is the
# simulation's `is_alive()` — never the HP number — and a dead character keeps its real pools,
# its diamond, and its name while the death slot switches on. Bars carry no labels and no
# numbers, names follow the locked fallback and truncation policy, and freeing what an overhead
# watches hides it instead of dereferencing it.
#
# Fixtures are real StandingDummy sheets, configured and damaged for real through C++ —
# explicitly named NPC fixtures and purpose-built empty-name cases exercise both name paths.

var failures := 0
var checks := 0

const WORLD_PATH := "res://scripts/world_status.gd"
const BAR_PATH := "res://scripts/status_bar.gd"

const ALLY_COLOR := Color("#4E7A4A")
const ENEMY_COLOR := Color("#8F423D")
const BAR_ROW_PATHS := ["BarStack/HP", "BarStack/Stamina", "BarStack/Mana"]
const CHILD_ROW_PATHS := ["HP", "Stamina", "Mana"]


func _initialize() -> void:
	call_deferred("_run")


func _same_colour(got: Color, want: Color) -> bool:
	return _close(got.r, want.r) and _close(got.g, want.g) and _close(got.b, want.b) \
			and _close(got.a, want.a)


func _check(condition: bool, message: String) -> void:
	checks += 1
	if not condition:
		printerr("FAIL: ", message)
		failures += 1


func _close(a: float, b: float, epsilon: float = 0.0001) -> bool:
	return absf(a - b) < epsilon


# --- fixtures ------------------------------------------------------------------

func _status() -> Control:
	var script: Script = load(WORLD_PATH)
	if script == null:
		_check(false, "the world status exists at %s" % WORLD_PATH)
		return null
	var host := Control.new()
	root.add_child(host)
	var control: Control = script.new()
	host.add_child(control)
	return control


func _dummy(faction: int, level: int, id: int, seed: int, character_name: String,
		node_name: String) -> StandingDummy:
	var dummy := StandingDummy.new()
	dummy.name = node_name
	dummy.setup(faction, level, id, seed, character_name)
	root.add_child(dummy)
	return dummy


func _label_text(status: Control, path: String) -> String:
	var label := status.get_node_or_null(path) as Label
	return "" if label == null else label.text


func _bar(status: Control, path: String) -> Control:
	return status.get_node_or_null(path)


func _free(node: Node) -> void:
	if node != null and node.get_parent() != null:
		node.get_parent().free()


# --- cases ---------------------------------------------------------------------

func _compact_plaque_contains_dead_identity_and_narrow_bars() -> void:
	var status := _status()
	var actor := _dummy(1, 7, 151, 751, "WWWWWWWWWWWWWWWWWWWW", "Wide")
	_check(actor.sheet.apply_damage(0, 25.0), "fixture dies")
	status.bind_actor(actor)
	status.refresh()
	await process_frame
	await process_frame
	var bounds := status.get_global_rect()
	_check(bounds.size.x <= 300.0 and bounds.size.y <= 80.0, "overhead plaque stays compact")
	for path in ["CharacterName", "RelationshipDiamond", "DeathIcon", "LevelBadge", "BarStack"] + BAR_ROW_PATHS:
		var control := status.get_node(path) as Control
		_check(bounds.encloses(control.get_global_rect()), "%s fits in the framed plaque" % path)
	var name_label := status.get_node("CharacterName") as Label
	var skull := status.get_node("DeathIcon") as Control
	_check(skull.visible and name_label.get_global_rect().end.x <= skull.get_global_rect().position.x,
			"the visible skull follows the name without overlap")
	_check(name_label.get_theme_font("font").get_string_size(name_label.text, HORIZONTAL_ALIGNMENT_LEFT,
			-1, name_label.get_theme_font_size("font_size")).x <= name_label.size.x,
			"all twenty name characters fit")
	for path in BAR_ROW_PATHS:
		var row := status.get_node(path) as Control
		_check(row.size.x <= 120.0, "%s is at most 120 pixels wide" % path)
		_check(not (row.get_node("Role") as Control).visible and not (row.get_node("Value") as Control).visible,
				"%s unused columns collapse" % path)
	_check(status.mouse_filter == Control.MOUSE_FILTER_IGNORE, "world plaque ignores input")
	for child in status.find_children("*", "Control", true, false):
		_check((child as Control).mouse_filter == Control.MOUSE_FILTER_IGNORE,
				"%s ignores input" % child.name)
	_free(status)
	actor.queue_free()


func _the_overhead_shows_the_actors_own_identity() -> void:
	var status := _status()
	if status == null:
		return
	var bar_script: Script = load(BAR_PATH)
	var alpha := _dummy(0, 1, 101, 701, "World Alpha", "AlphaBody")
	var beta := _dummy(1, 7, 102, 702, "World Beta", "BetaBody")
	status.bind_actor(alpha)
	status.refresh()
	for child_path in ["CharacterName", "RelationshipDiamond", "DeathIcon",
			"LevelBadge"] + BAR_ROW_PATHS:
		_check(status.has_node(child_path), "the overhead has %s" % child_path)
	_check(_label_text(status, "CharacterName") == "World Alpha",
			"the name is the actor's own sheet name")
	_check(_label_text(status, "LevelBadge/Value") == "Lv 1",
			"the badge reads Lv N from the actor's level")
	var diamond := status.get_node_or_null("RelationshipDiamond") as Control
	_check(diamond != null and diamond.get("diamond_color") == ALLY_COLOR,
			"the ally diamond is the locked muted green")
	for row_path in BAR_ROW_PATHS:
		var row := _bar(status, row_path)
		_check(row != null and bar_script != null and is_instance_of(row, bar_script),
				"%s is a shared StatusBar" % row_path)
		if row != null:
			var role := row.get_node_or_null("Role") as Label
			var value := row.get_node_or_null("Value") as Label
			_check(role != null and role.text == "" and value != null and value.text == "",
					"%s shows no label and no numbers" % row_path)
	var hp := _bar(status, BAR_ROW_PATHS[0])
	var stamina := _bar(status, BAR_ROW_PATHS[1])
	var mana := _bar(status, BAR_ROW_PATHS[2])
	_check(hp != null and stamina != null and mana != null
			and hp.position.y < stamina.position.y and stamina.position.y < mana.position.y,
			"the bars stack in the locked HP, stamina, mana order")
	var stack := status.get_node_or_null("BarStack") as Control
	var badge := status.get_node_or_null("LevelBadge") as Control
	var badge_text := status.get_node_or_null("LevelBadge/Value") as Label
	_check(badge_text != null and badge_text.vertical_alignment == VERTICAL_ALIGNMENT_CENTER,
			"level text is vertically centered inside the badge")
	_check(stack != null and badge != null
			and badge.position.x >= stack.position.x + stack.size.x,
			"the level badge sits right of the bar stack")
	var strength_found := false
	for node in status.find_children("*", "Label", true, false):
		if "rength" in (node as Label).text:
			strength_found = true
	_check(not strength_found, "no strength text survives on the overhead")
	# Rebinding switches the whole identity, and it is the other sheet's numbers underneath.
	status.bind_actor(beta)
	status.refresh()
	_check(_label_text(status, "CharacterName") == "World Beta", "the name followed the actor")
	_check(_label_text(status, "LevelBadge/Value") == "Lv 7", "the badge followed the level")
	_check(diamond.get("diamond_color") == ENEMY_COLOR, "the enemy diamond is the locked red")
	_check(hp.get("ratio") > 0.0, "bars mirror the new actor, not the old one")
	_free(status)
	alpha.queue_free()
	beta.queue_free()


func _an_attack_order_leaves_the_diamond_alone() -> void:
	var status := _status()
	if status == null:
		return
	var ally := _dummy(0, 1, 111, 711, "Order Giver", "GiverBody")
	var foe := _dummy(1, 1, 112, 712, "Order Target", "TargetBody")
	var combat := OpenCombat.new()
	_check(combat.add_character(ally.sheet, Vector3.ZERO), "the ally joins the session")
	_check(combat.add_character(foe.sheet, Vector3(2.0, 0.0, 0.0)), "the foe joins the session")
	_check(combat.issue_attack(111, 112), "the ally is ordered to attack")
	_check(int(ally.sheet.get_attack_target_id()) == 112, "C++ really holds the attack order")
	status.bind_actor(ally)
	status.refresh()
	var diamond := status.get_node_or_null("RelationshipDiamond") as Control
	_check(diamond != null and diamond.get("diamond_color") == ALLY_COLOR,
			"hostility is not a faction: the diamond stayed green")
	_free(status)
	ally.queue_free()
	foe.queue_free()


func _fills_and_death_follow_the_real_sheet() -> void:
	var status := _status()
	if status == null:
		return
	var hurt := _dummy(0, 1, 121, 721, "Hurt", "HurtBody")
	_check(hurt.sheet.apply_damage(2, 25.0), "the leg takes twenty-five")
	status.bind_actor(hurt)
	status.refresh()
	_check(_close(float(_bar(status, BAR_ROW_PATHS[0]).get("ratio")), 75.0 / 100.0),
			"one refresh, and the fill is the new truth")
	_check((_bar(status, BAR_ROW_PATHS[0]).get_node("Value") as Label).text == "",
			"and still no numbers appear on world bars")

	var dead := _dummy(1, 1, 122, 722, "Dead Man", "DeadBody")
	_check(dead.sheet.apply_damage(0, 25.0), "the head takes its full health")
	status.bind_actor(dead)
	status.refresh()
	_check(not bool(dead.sheet.is_alive()), "the sheet is dead")
	_check((status.get_node("DeathIcon") as CanvasItem).visible,
			"the death slot is on, driven by is_alive()")
	_check((status.get_node("DeathIcon") as TextureRect).texture != null,
			"the death icon really draws the approved authored skull artwork")
	_check(status.has_node("RelationshipDiamond")
			and (status.get_node("RelationshipDiamond") as CanvasItem).visible,
			"the diamond stays present for the dead")
	_check(_close(float(_bar(status, BAR_ROW_PATHS[0]).get("ratio")), 75.0 / 100.0),
			"a head-death corpse keeps its real 75-of-100 main fill")
	_check(_label_text(status, "CharacterName") == "Dead Man", "the dead keep their names")
	var child_count := status.get_child_count()
	status.refresh()
	status.refresh()
	_check(status.get_child_count() == child_count,
			"repeated refreshes never stack duplicate bars or icons")
	var bar_children: Node = status.get_node("BarStack")
	_check(bar_children.get_child_count() == 3, "the bar stack stays three rows deep")

	var tiny := _dummy(0, 1, 123, 723, "Nearly", "NearlyBody")
	_check(tiny.sheet.apply_damage(2, 40.0) and tiny.sheet.apply_damage(3, 40.0)
			and tiny.sheet.apply_damage(4, 19.999), "the limbs take everything")
	status.bind_actor(tiny)
	status.refresh()
	_check(bool(tiny.sheet.is_alive()), "main health is tiny but the sheet lives")
	_check(not (status.get_node("DeathIcon") as CanvasItem).visible,
			"a living sliver of HP wears no skull")
	_check(float(_bar(status, BAR_ROW_PATHS[0]).get("ratio")) > 0.0,
			"and the fill still knows it is not empty")
	_free(status)
	hurt.queue_free()
	dead.queue_free()
	tiny.queue_free()


func _names_follow_the_locked_policy() -> void:
	var status := _status()
	if status == null:
		return
	var nameless := _dummy(0, 1, 131, 731, "", "Hollow Guardian")
	status.bind_actor(nameless)
	status.refresh()
	_check(_label_text(status, "CharacterName") == "Hollow Guardian",
			"an empty sheet name falls back to the scene node name")
	var long_name := "Twenty-six characters long name"
	var poet := _dummy(1, 1, 132, 732, long_name, "PoetBody")
	status.bind_actor(poet)
	status.refresh()
	_check(_label_text(status, "CharacterName") == long_name.substr(0, 19) + "…",
			"a long name truncates to nineteen and an ellipsis")
	_check(_label_text(status, "CharacterName") != long_name,
			"the full twenty-six never reach the screen")
	_free(status)
	nameless.queue_free()
	poet.queue_free()


func _binding_ownership_is_safe_and_idempotent() -> void:
	var status := _status()
	if status == null:
		return
	var first := _dummy(0, 1, 141, 741, "First", "FirstBody")
	status.bind_actor(first)
	status.bind_actor(first)
	status.refresh()
	var name_labels: Array = status.find_children("CharacterName", "Label", true, false)
	_check(name_labels.size() == 1, "binding twice builds one identity, never two")
	var replacement := OpenCharacter.new()
	_check(replacement.configure_npc(199, "Replaced Sheet", 1, 3, 909),
			"a replacement fixture sheet configures")
	var before_name := String(first.sheet.get_character_name())
	first.sheet = replacement
	status.refresh()
	_check(_label_text(status, "CharacterName") == "Replaced Sheet",
			"refresh resolves the actor's current sheet, not the first one")
	_check(String(replacement.get_character_name()) == "Replaced Sheet",
			"the new sheet survived the read")
	_check(before_name == "First", "the old sheet is untouched by the swap")
	status.bind_actor(null)
	status.refresh()
	_check(not status.visible, "unbinding hides the overhead without a crash")
	var doomed := _dummy(1, 1, 142, 742, "Doomed", "DoomedBody")
	status.bind_actor(doomed)
	status.refresh()
	_check(status.visible, "a bound overhead shows")
	doomed.queue_free()
	await process_frame
	await process_frame
	var crashed := false
	status.refresh()
	if status.visible:
		crashed = true
	_check(not crashed, "a freed actor hides its overhead instead of dereferencing it")
	_check(not status.visible, "and it stays hidden, quietly")
	_free(status)
	first.queue_free()


# The overhead plate is three painted layers, thinner than the player's plaque on purpose: the
# cast band at the edge, the muted brass 2 px in, wood 1 px further. A test that only checked
# the constants could not tell this frame from the plaque's 3/5 one, which is exactly the drift
# the shared palette was locked to prevent.
func _the_plate_paints_band_brass_and_wood_in_that_order() -> void:
	var status := _status()
	if status == null:
		return
	var script: Script = status.get_script()
	_check(script.has_method("paint_layers"),
			"the overhead's paint stack is a function, so the drawn order is assertable")
	if not script.has_method("paint_layers"):
		_free(status)
		return
	var layers: Array = script.paint_layers(Vector2(176, 48))
	_check(layers.size() == 3, "band, brass and wood, got %d" % layers.size())
	if layers.size() != 3:
		_free(status)
		return
	var wants: Array = [[Color("#2E231C"), 0.0], [Color("#806343"), 2.0], [Color("#241B16"), 3.0]]
	for index in range(3):
		var rect: Rect2 = layers[index]["rect"]
		_check(_same_colour(layers[index]["color"], wants[index][0]),
				"layer %d is %s: got %s" % [index, str(wants[index][0]), str(layers[index]["color"])])
		var inset := float(wants[index][1])
		_check(_close(rect.position.x, inset) and _close(rect.position.y, inset)
				and _close(rect.end.x, 176.0 - inset) and _close(rect.end.y, 48.0 - inset),
				"layer %d is inset %f on every side: %s" % [index, inset, str(rect)])
	_free(status)


# The shared bar, bottom layer first: the sunken track, its one-pixel frame, the fill inside
# both and the two-pixel top highlight. A full bar fills the whole inside; an empty bar is the
# bare track, so ratio 0 paints only the first two layers. Nothing rasterises in a headless
# run, so the submitted stack is the evidence that the fill cannot cover its own frame.
func _the_bar_paints_track_frame_fill_and_highlight() -> void:
	var script: Script = load(BAR_PATH)
	_check(script != null, "the shared bar exists at %s" % BAR_PATH)
	if script == null:
		return
	_check(script.has_method("paint_layers"),
			"the bar's paint stack is a function, so the drawn order is assertable")
	if not script.has_method("paint_layers"):
		return
	var full: Array = script.paint_layers(Vector2(120, 6), 0.0, 0.0, 1.0, Color("#8F423D"))
	_check(full.size() == 4, "track, frame, fill and highlight, got %d" % full.size())
	if full.size() != 4:
		return
	_check(full[0]["rect"] == Rect2(0.0, 0.0, 120.0, 6.0),
			"the track spans the row at the locked six-pixel height")
	_check(_same_colour(full[0]["color"], Color("#2E231C")), "the track is sunken brown")
	_check(bool(full[1].get("filled", true)) == false
			and _close(float(full[1].get("width", 0.0)), 1.0),
			"the frame is a one-pixel stroke over the track, not a filled block")
	_check(_same_colour(full[1]["color"], Color("#6E5539")), "and it is muted brass")
	_check(full[2]["rect"] == Rect2(1.0, 1.0, 118.0, 4.0),
			"a full bar fills the inside of its frame: %s" % str(full[2]["rect"]))
	_check(_same_colour(full[2]["color"], Color("#8F423D")), "in the pool's own locked shade")
	_check(full[3]["rect"] == Rect2(1.0, 1.0, 118.0, 2.0),
			"the highlight is the top two pixels of the fill: %s" % str(full[3]["rect"]))
	_check(_same_colour(full[3]["color"], Color(1.0, 1.0, 1.0, 0.14)),
			"and it is the soft white the palette locks")
	var half: Array = script.paint_layers(Vector2(120, 6), 0.0, 0.0, 0.5, Color("#4E7A4A"))
	_check(half.size() == 4 and _close((half[2]["rect"] as Rect2).size.x, 59.0),
			"half a pool is half the inside, got %s" % str(half[2]["rect"] if half.size() > 2 else null))
	_check(_same_colour(half[2]["color"], Color("#4E7A4A")), "in the stamina shade it was given")
	var empty: Array = script.paint_layers(Vector2(120, 6), 0.0, 0.0, 0.0, Color("#4A6690"))
	_check(empty.size() == 2, "an empty bar is the bare track and its frame, got %d" % empty.size())


# --- runner --------------------------------------------------------------------

func _case(what: String, minimum: int, callable: Callable) -> void:
	var before := checks
	await callable.call()
	var made := checks - before
	_check(made >= minimum, "%s ran its own checks: %d against a floor of %d" % [what, made,
			minimum])


func _run() -> void:
	await _case("compact plaque contains dead identity and narrow bars", 18,
			_compact_plaque_contains_dead_identity_and_narrow_bars)
	await _case("the overhead shows the actor's own identity", 23,
			_the_overhead_shows_the_actors_own_identity)
	await _case("an attack order leaves the diamond alone", 5,
			_an_attack_order_leaves_the_diamond_alone)
	await _case("fills and death follow the real sheet", 14,
			_fills_and_death_follow_the_real_sheet)
	await _case("names follow the locked policy", 3, _names_follow_the_locked_policy)
	await _case("binding ownership is safe and idempotent", 9,
			_binding_ownership_is_safe_and_idempotent)
	await _case("band, brass and wood in that order", 8,
			_the_plate_paints_band_brass_and_wood_in_that_order)
	await _case("the bar draws track, frame, fill and highlight", 12,
			_the_bar_paints_track_frame_fill_and_highlight)

	_check(checks > 75, "the suite ran its full set of checks (ran %d)" % checks)
	if failures == 0:
		print("World status: all checks passed")
	quit(1 if failures else 0)
