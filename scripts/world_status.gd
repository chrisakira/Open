# The world overhead: one character's own identity and pools, floating above them.
#
# Everything here mirrors the *actor's current sheet*, freshly resolved every refresh — not a
# cached sheet, not the player's numbers, and never a second opinion about life or death. The
# relationship diamond is the C++ faction and nothing else: being attacked, attacking back, or
# how the fight is going cannot recolour it (locked). The death icon is `is_alive()` and nothing
# else: a head-death corpse still shows its real 75-of-100 main fill next to the skull, and a
# living sliver of health wears no skull. Names follow the locked policy (sheet name, else the
# scene node name; truncate past twenty) through the presenter.
#
# The bars are the shared StatusBar with its label and value columns collapsed: three unlabeled,
# numberless fills in the locked order. The level badge is the human's brass-framed plate. This
# control does not decide *visibility* — distance and cover are the overlay's gated business;
# it only ever hides itself when its actor or sheet is gone (G6: hide, never guess).

extends Control

const Presenter := preload("res://scripts/status_presenter.gd")
const Bar := preload("res://scripts/status_bar.gd")

const ALLY_COLOR := Bar.STAMINA_COLOR # diamonds clone the bar shades (human B4)
const ENEMY_COLOR := Bar.HP_COLOR
const NAME_COLOR := Color("#F4E6C6")
const BADGE_WOOD := Color("#493326")
const BADGE_BRASS := Color("#806343")
const BADGE_TEXT := Color("#F4E6C6")
const SKULL_PATH := "res://assets/ui/status_skull.svg"

const NAME_ROW_HEIGHT := 18.0
const DIAMOND_SIZE := 9.0
const STACK_WIDTH := 120.0
const BAR_ROW_HEIGHT := 6.0
const BAR_ROW_GAP := 2.0
const STACK_HEIGHT := 3.0 * BAR_ROW_HEIGHT + 2.0 * BAR_ROW_GAP
const BADGE_WIDTH := 38.0
const BADGE_HEIGHT := 18.0
const GAP := 4.0

var _actor: CharacterBody3D

var _name: Label
var _diamond: Control
var _skull: TextureRect
var _stack: Control
var _rows: Dictionary = {}
var _badge: Panel
var _badge_label: Label


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = Vector2(180.0, NAME_ROW_HEIGHT + 2.0 + STACK_HEIGHT)

	_diamond = _Diamond.new()
	_diamond.name = "RelationshipDiamond"
	add_child(_diamond)

	_name = Label.new()
	_name.name = "CharacterName"
	_name.add_theme_color_override("font_color", NAME_COLOR)
	add_child(_name)

	_skull = TextureRect.new()
	_skull.name = "DeathIcon"
	_skull.custom_minimum_size = Vector2(13.0, 13.0)
	_skull.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_CENTERED
	_skull.visible = false
	add_child(_skull)

	_stack = Control.new()
	_stack.name = "BarStack"
	add_child(_stack)
	for row_spec in [["HP", Bar.HP_COLOR], ["Stamina", Bar.STAMINA_COLOR],
			["Mana", Bar.MANA_COLOR]]:
		var row: Control = Bar.new()
		row.name = String(row_spec[0])
		row.fill_color = row_spec[1]
		row.role_width = 0.0
		row.value_width = 0.0
		_stack.add_child(row)
		_rows[String(row_spec[0])] = row

	_badge = Panel.new()
	_badge.name = "LevelBadge"
	var plate := StyleBoxFlat.new()
	plate.bg_color = BADGE_WOOD
	plate.border_color = BADGE_BRASS
	plate.set_border_width_all(1)
	plate.set_corner_radius_all(2)
	plate.content_margin_left = 5.0
	plate.content_margin_right = 5.0
	_badge.add_theme_stylebox_override("panel", plate)
	_badge_label = Label.new()
	_badge_label.name = "Value"
	_badge_label.add_theme_color_override("font_color", BADGE_TEXT)
	_badge_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_badge.add_child(_badge_label)
	add_child(_badge)


func _ready() -> void:
	Bar.ignore_input_tree(self)
	_skull.texture = load(SKULL_PATH)


# The overlay owns one of these per actor. Binding is idempotent: the control tree is built
# once in _init, so re-binding can only ever swap what is mirrored, never what exists.
func bind_actor(actor: CharacterBody3D) -> void:
	_actor = actor


# One frame of honest mirroring: resolve the actor's *current* sheet, print what it says now.
func refresh() -> void:
	if _actor == null or not is_instance_valid(_actor):
		visible = false
		return
	var sheet: OpenCharacter = _actor.get("sheet")
	if sheet == null or not is_instance_valid(sheet) or not sheet.is_configured():
		visible = false
		return
	visible = true
	_name.text = Presenter.display_name(String(sheet.get_character_name()), String(_actor.name))
	_diamond.set("diamond_color",
			ALLY_COLOR if int(sheet.get_faction()) == 0 else ENEMY_COLOR)
	_skull.visible = not bool(sheet.is_alive())
	_badge_label.text = "Lv %d" % int(sheet.get_level())
	_present_row("HP", float(sheet.get_health()), float(sheet.get_max_health()))
	_present_row("Stamina", float(sheet.get_stamina()), float(sheet.get_max_stamina()))
	_present_row("Mana", float(sheet.get_mana()), float(sheet.get_max_mana()))
	_layout()


func _present_row(row_name: String, current: float, maximum: float) -> void:
	var row: Control = _rows[row_name]
	row.present(current, maximum, "")


func _layout() -> void:
	_diamond.size = Vector2(DIAMOND_SIZE, DIAMOND_SIZE)
	_diamond.position = Vector2(0.0, (NAME_ROW_HEIGHT - DIAMOND_SIZE) / 2.0)
	_name.reset_size()
	_name.position = Vector2(DIAMOND_SIZE + GAP, 0.0)
	_skull.position = Vector2(_name.position.x + _name.size.x + GAP + 1.0,
			(NAME_ROW_HEIGHT - _skull.custom_minimum_size.y) / 2.0)
	_skull.size = _skull.custom_minimum_size
	_stack.position = Vector2(0.0, NAME_ROW_HEIGHT + 2.0)
	_stack.size = Vector2(STACK_WIDTH, STACK_HEIGHT)
	var y := 0.0
	for row_name in ["HP", "Stamina", "Mana"]:
		var row: Control = _rows[row_name]
		row.position = Vector2(0.0, y)
		row.size = Vector2(STACK_WIDTH, BAR_ROW_HEIGHT)
		row.queue_redraw()
		y += BAR_ROW_HEIGHT + BAR_ROW_GAP
	_badge.position = Vector2(STACK_WIDTH + GAP + 2.0,
			NAME_ROW_HEIGHT + 2.0 + (STACK_HEIGHT - BADGE_HEIGHT) / 2.0)
	_badge.size = Vector2(BADGE_WIDTH, BADGE_HEIGHT)
	var skull_right := _skull.position.x + _skull.custom_minimum_size.x
	var root_width := maxf(skull_right, _badge.position.x + _badge.size.x)
	custom_minimum_size = Vector2(root_width, NAME_ROW_HEIGHT + 2.0 + STACK_HEIGHT)
	reset_size()


# A quiet rotated square. A Control rather than a texture because it is one polygon of the
# locked identity, and its colour is set from the faction every single refresh.
class _Diamond:
	extends Control

	var diamond_color := Color.WHITE:
		set(value):
			diamond_color = value
			queue_redraw()

	func _init() -> void:
		mouse_filter = Control.MOUSE_FILTER_IGNORE
		custom_minimum_size = Vector2(9.0, 9.0)

	func _draw() -> void:
		var c := size / 2.0
		var half := minf(c.x, c.y) - 0.5
		draw_colored_polygon(PackedVector2Array([
			Vector2(c.x, c.y - half), Vector2(c.x + half, c.y),
			Vector2(c.x, c.y + half), Vector2(c.x - half, c.y)]), diamond_color)
