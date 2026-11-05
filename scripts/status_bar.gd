# The shared status bar: one row — a role label, a sunken beveled bar, and a readout.
#
# Both views use it: the player plaque passes a role name and the `90/100` readout,
# the world overhead passes empty strings and gets a bare bar. It stores only render values —
# the latest ratio, color, and text — never a pool it could call its own, and it fills
# immediately: whatever `present` is told is what the next draw shows, with no tween, drain,
# or refill anywhere near it (locked). The locked G5 palette lives here so the two views cannot
# drift apart: deep fills, sunken `#2E231C` track, `#6E5539` frame, a two-pixel top highlight.
#
# Zero and negative maxima draw an empty bar instead of dividing (G6, human 2026-11-01: fill
# empty, print the truth — printing stays the caller's readout, which this mirrors).

extends Control

const Presenter := preload("res://scripts/status_presenter.gd")

const TRACK := Color("#2E231C")
const FRAME := Color("#6E5539")
const HIGHLIGHT := Color(1.0, 1.0, 1.0, 0.14)

# The locked resource palette (G5, human 2026-11-01) lives with the bar both views share, so
# the plaque, the overhead and the relationship diamonds cannot drift apart.
const HP_COLOR := Color("#8F423D")
const STAMINA_COLOR := Color("#4E7A4A")
const MANA_COLOR := Color("#4A6690")

const ROLE_WIDTH := 62.0
const VALUE_WIDTH := 126.0
const BAR_HEIGHT := 10.0
const GAP := 6.0

var fill_color := Color.WHITE
var role_name := ""
# World rows pass neither label nor numbers: their columns collapse to zero width.
var role_width := ROLE_WIDTH
var value_width := VALUE_WIDTH
# What `present` last showed. Public so tests (and the overhead) can read the truth of a frame.
var ratio := 0.0

var _role: Label
var _value: Label


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = Vector2(0.0, 0.0)
	var serif := SystemFont.new()
	serif.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	_role = Label.new()
	_role.name = "Role"
	_role.add_theme_font_override("font", serif)
	_role.add_theme_font_size_override("font_size", 13)
	_role.add_theme_color_override("font_color", Color("#E0D0AE"))
	_value = Label.new()
	_value.name = "Value"
	_value.add_theme_font_override("font", serif)
	_value.add_theme_font_size_override("font_size", 13)
	_value.add_theme_color_override("font_color", Color("#F0D3A0"))
	_value.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	add_child(_role)
	add_child(_value)


func _ready() -> void:
	_role.text = role_name
	_layout_row()
	ignore_input_tree(self)


# Every descendant of a status surface is click-through, decorative or not (locked): one place
# says so, so no future child can quietly eat a floor click.
static func ignore_input_tree(node: Node) -> void:
	for child in node.get_children():
		if child is Control:
			(child as Control).mouse_filter = Control.MOUSE_FILTER_IGNORE
		ignore_input_tree(child)


func present(current: float, maximum: float, text: String) -> void:
	ratio = Presenter.pool_fraction(current, maximum) if maximum > 0.0 else 0.0
	_value.text = text
	if value_width > 0.0:
		value_width = maxf(VALUE_WIDTH, _value.get_combined_minimum_size().x)
		_layout_row()
	queue_redraw()


func _layout_row() -> void:
	if _role == null or _value == null:
		return
	_role.visible = role_width > 0.0
	_value.visible = value_width > 0.0
	_role.size = Vector2(role_width, size.y) if _role.visible else Vector2.ZERO
	_value.size = Vector2(value_width, size.y) if _value.visible else Vector2.ZERO
	_role.position = Vector2(0.0, maxf(0.0, (size.y - _role.size.y) / 2.0))
	_value.position = Vector2(size.x - value_width, maxf(0.0, (size.y - _value.size.y) / 2.0))


func _notification(what: int) -> void:
	if what == Control.NOTIFICATION_RESIZED:
		_layout_row()
		queue_redraw()


# The bar sits between the two columns (whichever exist), vertically centred in the row.
# The painted stack, bottom layer first: the sunken track, its one-pixel frame, then the fill
# inside both and the two-pixel top highlight. An empty bar stops after the frame, which is how
# the locked "bare track" rule is drawn. The stack is a function because nothing rasterises in
# a headless run, so the order and the insets are the only evidence a suite can hold.
static func paint_layers(bar_size: Vector2, role_w: float, value_w: float, ratio_value: float,
		colour: Color) -> Array:
	var bar_left := role_w + (GAP if role_w > 0.0 else 0.0)
	var bar_right := bar_size.x - value_w - (GAP if value_w > 0.0 else 0.0)
	if bar_right <= bar_left:
		return []
	var bar_height := minf(BAR_HEIGHT, bar_size.y)
	var top := (bar_size.y - bar_height) / 2.0
	var track := Rect2(bar_left, top, bar_right - bar_left, bar_height)
	var layers: Array = [
		{"rect": track, "color": TRACK},
		{"rect": track, "color": FRAME, "filled": false, "width": 1.0},
	]
	if ratio_value > 0.0:
		var inner := track.grow(-1.0)
		var fill := Rect2(inner.position,
				Vector2(inner.size.x * clampf(ratio_value, 0.0, 1.0), inner.size.y))
		layers.append({"rect": fill, "color": colour})
		layers.append({
			"rect": Rect2(fill.position, Vector2(fill.size.x, minf(2.0, inner.size.y))),
			"color": HIGHLIGHT,
		})
	return layers


func _draw() -> void:
	for layer in paint_layers(size, role_width, value_width, ratio, fill_color):
		draw_rect(layer["rect"] as Rect2, layer["color"] as Color,
				bool(layer.get("filled", true)), float(layer.get("width", 1.0)))
