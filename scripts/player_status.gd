# The player's plaque: identity over three labelled bars, bottom-centre, click-through.
#
# This is the human's locked plaque (status_UID.md, G5 decisions 2026-11-01): solid opaque wood
# `#241B16` in a brass frame with a dark cast band, 16 px above the bottom edge,
# always bottom-centre; the action chat narrows to clear it at tested viewport sizes.
# The name and the `Level N` line mirror the sheet through the presenter; the three rows mirror
# the three pools. It never ticks, spends, drains, caches a maximum or infers death: every
# refresh re-reads the bridge, and an unbound or unconfigured sheet makes the whole plaque hide
# rather than show a half-truth (G6, hidden by decision).
#
# No skull lives here: a player death overlay was never locked (plan Task 2, human).

extends Control

const Presenter := preload("res://scripts/status_presenter.gd")
const Bar := preload("res://scripts/status_bar.gd")

const HP_COLOR := Bar.HP_COLOR
const STAMINA_COLOR := Bar.STAMINA_COLOR
const MANA_COLOR := Bar.MANA_COLOR
const WOOD := Color("#241B16")
const BRASS := Color("#806343")
const CAST_BAND := Color("#2E231C")
const NAME_COLOR := Color("#F4E6C6")
const LEVEL_COLOR := Color("#BD9862")

const PLAQUE_SIZE := Vector2(440.0, 108.0)
const BOTTOM_GAP := 16.0
const SIDE_PADDING := 12.0
const IDENTITY_HEIGHT := 22.0
const ROW_HEIGHT := 20.0
const ROW_GAP := 2.0

var _sheet: OpenCharacter
var _fallback_name := ""

var _identity: HBoxContainer
var _name: Label
var _level: Label
var _rows: Dictionary = {}


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = PLAQUE_SIZE
	size = PLAQUE_SIZE
	# Bottom-centre of whatever canvas holds us; grows upward, never drifts sideways.
	set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM)
	offset_left = -PLAQUE_SIZE.x / 2.0
	offset_right = PLAQUE_SIZE.x / 2.0
	offset_top = -PLAQUE_SIZE.y - BOTTOM_GAP
	offset_bottom = -BOTTOM_GAP

	_identity = HBoxContainer.new()
	_identity.name = "Identity"
	_identity.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_name = Label.new()
	_name.name = "CharacterName"
	var serif := SystemFont.new()
	serif.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	_name.add_theme_font_override("font", serif)
	_name.add_theme_font_size_override("font_size", 14)
	_name.add_theme_color_override("font_color", NAME_COLOR)
	_level = Label.new()
	_level.name = "Level"
	_level.add_theme_font_override("font", serif)
	_level.add_theme_font_size_override("font_size", 13)
	_level.add_theme_color_override("font_color", LEVEL_COLOR)
	_level.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_level.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	_identity.add_child(_name)
	_identity.add_child(_level)
	add_child(_identity)

	for row_spec in [["HP", HP_COLOR], ["Stamina", STAMINA_COLOR], ["Mana", MANA_COLOR]]:
		var row: Control = Bar.new()
		row.name = String(row_spec[0])
		row.role_name = String(row_spec[0])
		row.fill_color = row_spec[1]
		add_child(row)
		_rows[String(row_spec[0])] = row


func _ready() -> void:
	Bar.ignore_input_tree(self)
	_layout()


# Bind the sheet this plaque mirrors. `fallback_name` is the G3 rule's second choice (the
# scene-node name); it defaults empty because the player's sheet always has a name today.
func bind_sheet(sheet: OpenCharacter, fallback_name := "") -> void:
	_sheet = sheet
	_fallback_name = fallback_name


# The per-frame seam: read the bridge now, show that now. Hidden rather than guessed at when
# there is no configured sheet to mirror.
func refresh() -> void:
	if _sheet == null or not _sheet.is_configured():
		visible = false
		return
	visible = true
	_name.text = Presenter.display_name(String(_sheet.get_character_name()), _fallback_name)
	_level.text = "Level %d" % int(_sheet.get_level())
	_present_row("HP", _sheet.get_health(), _sheet.get_max_health())
	_present_row("Stamina", _sheet.get_stamina(), _sheet.get_max_stamina())
	_present_row("Mana", _sheet.get_mana(), _sheet.get_max_mana())


func _present_row(row_name: String, current: float, maximum: float) -> void:
	var row: Control = _rows[row_name]
	row.present(float(current), float(maximum), Presenter.pool_amounts(float(current), float(maximum)))


func _notification(what: int) -> void:
	if what == Control.NOTIFICATION_RESIZED:
		_layout()


func _layout() -> void:
	_identity.position = Vector2(SIDE_PADDING, SIDE_PADDING - 2.0)
	_identity.size = Vector2(size.x - 2.0 * SIDE_PADDING, IDENTITY_HEIGHT)
	var y := SIDE_PADDING + IDENTITY_HEIGHT + 2.0
	for row_name in ["HP", "Stamina", "Mana"]:
		var row: Control = _rows[row_name]
		row.position = Vector2(SIDE_PADDING, y)
		row.size = Vector2(size.x - 2.0 * SIDE_PADDING, ROW_HEIGHT)
		y += ROW_HEIGHT + ROW_GAP


# The locked plaque surface: dark wood, brass frame, dark cast band hugging the frame.
func _draw() -> void:
	var full := Rect2(Vector2.ZERO, size)
	draw_rect(full, CAST_BAND)
	var framed := full.grow(-3.0)
	draw_rect(framed, BRASS)
	draw_rect(framed.grow(-2.0), WOOD)
