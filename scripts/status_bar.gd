# The shared status bar: one row — a role label, a sunken beveled bar, and a readout.
#
# Both views use it: the player plaque passes a role name and the `90.00 / 100.00` readout,
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
const VALUE_WIDTH := 100.0
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
	custom_minimum_size = Vector2(340.0, 16.0)
	_role = Label.new()
	_role.name = "Role"
	_role.custom_minimum_size = Vector2(ROLE_WIDTH, 0.0)
	_value = Label.new()
	_value.name = "Value"
	_value.custom_minimum_size = Vector2(VALUE_WIDTH, 0.0)
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
	queue_redraw()


func _layout_row() -> void:
	_role.position = Vector2(0.0, maxf(0.0, (size.y - _role.size.y) / 2.0))
	_value.position = Vector2(size.x - value_width, maxf(0.0, (size.y - _value.size.y) / 2.0))


func _notification(what: int) -> void:
	if what == Control.NOTIFICATION_RESIZED:
		_layout_row()
		queue_redraw()


# The bar sits between the two columns (whichever exist), vertically centred in the row.
func _draw() -> void:
	var bar_left := role_width + (GAP if role_width > 0.0 else 0.0)
	var bar_right := size.x - value_width - (GAP if value_width > 0.0 else 0.0)
	if bar_right <= bar_left:
		return
	var bar_height := minf(BAR_HEIGHT, size.y)
	var top := (size.y - bar_height) / 2.0
	var track := Rect2(bar_left, top, bar_right - bar_left, bar_height)
	draw_rect(track, TRACK)
	draw_rect(track, FRAME, false, 1.0)
	if ratio > 0.0:
		var inner := track.grow(-1.0)
		var fill := Rect2(inner.position,
				Vector2(inner.size.x * clampf(ratio, 0.0, 1.0), inner.size.y))
		draw_rect(fill, fill_color)
		draw_rect(Rect2(fill.position, Vector2(fill.size.x, minf(2.0, inner.size.y))), HIGHLIGHT)
