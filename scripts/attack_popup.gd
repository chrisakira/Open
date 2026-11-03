class_name AttackPopup
extends PanelContainer

# The actions a clicked character offers: Attack, and - only for a character whose
# recruitable flag is set - Recruit. This view holds no combat or recruiting state and
# issues no order. It remembers only the id of the character that was clicked and
# whether that character could be asked; confirming emits and closes, and the world
# asks C++ whether the target is still valid. Escape closes it, and a click anywhere
# outside it closes it too while still reaching world picking.
#
# `open_for_target` takes the flag as a third argument, defaulted to false: every call
# site that predates recruiting still opens an Attack-only panel, and the new one adds
# the new action rather than replacing the old. Whether the flag is set is C++'s
# answer; nothing here decides it, and opening or closing recruits nobody.

signal attack_requested(target_id: int)
signal recruit_requested(target_id: int)

const INK := Color("e0d0ae")
const GOLD := Color("d0a96b")
const PANEL_COLOR := Color("241b16")
const BORDER_COLOR := Color("806343")

# A nudge clear of the pointer, so the panel never opens under the click that
# summoned it.
const OPEN_OFFSET := Vector2(10.0, 6.0)

var _target_id := 0
var _recruitable := false
var _button: Button
var _recruit_button: Button


func _ready() -> void:
	custom_minimum_size = Vector2(112.0, 34.0)
	# STOP, not an overlay: only the panel's own rectangle is consumed.
	mouse_filter = Control.MOUSE_FILTER_STOP
	modulate.a = 0.94
	visible = false
	var panel_style := StyleBoxFlat.new()
	panel_style.bg_color = PANEL_COLOR
	panel_style.border_color = BORDER_COLOR
	panel_style.set_border_width_all(2)
	panel_style.set_corner_radius_all(3)
	panel_style.content_margin_left = 6
	panel_style.content_margin_right = 6
	panel_style.content_margin_top = 5
	panel_style.content_margin_bottom = 5
	panel_style.shadow_color = Color(0.03, 0.02, 0.01, 0.6)
	panel_style.shadow_size = 8
	add_theme_stylebox_override("panel", panel_style)
	_build_actions()


func open_for_target(target_id: int, screen_position: Vector2, recruitable: bool = false) -> void:
	# An id that cannot name a character gets no panel: a visible Attack button that
	# can never order anything would be a lie about what is clickable.
	if target_id <= 0:
		close()
		return
	_target_id = target_id
	# The flag arrives with the click, from the sheet C++ is holding. A panel that
	# advertised Recruit for a sheet without it would offer an action the coordinator
	# refuses, which is exactly the lie `docs/style.md` says a popup must not tell.
	_recruitable = recruitable
	_recruit_button.visible = recruitable
	visible = true
	reset_size()
	position = _clamped_position(screen_position)


func close() -> void:
	_target_id = 0
	_recruitable = false
	# A closed panel offers nothing, and that is not left to the panel's own
	# invisibility: a Recruit button still showing from the last ask would be offered to
	# whoever is clicked next, flag or no flag.
	if _recruit_button != null:
		_recruit_button.visible = false
	visible = false


func covers(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func _unhandled_key_input(event: InputEvent) -> void:
	if not visible:
		return
	if event is InputEventKey and event.pressed and not event.echo \
			and (event.keycode == KEY_ESCAPE or event.physical_keycode == KEY_ESCAPE):
		close()
		get_viewport().set_input_as_handled()


func _on_attack_pressed() -> void:
	if not visible or _target_id <= 0:
		return
	var target := _target_id
	close()
	attack_requested.emit(target)


func _on_recruit_pressed() -> void:
	if not visible or _target_id <= 0 or not _recruitable:
		# A hidden button cannot be the one that answered, and this panel does not
		# decide that a sheet without the flag is askable.
		return
	var target := _target_id
	close()
	recruit_requested.emit(target)


# The panel belongs beside the click, but a click near an edge must not push its
# buttons off screen.
func _clamped_position(screen_position: Vector2) -> Vector2:
	var wanted := screen_position + OPEN_OFFSET
	var limit := get_viewport_rect().size - size
	return Vector2(
		clampf(wanted.x, 0.0, maxf(0.0, limit.x)),
		clampf(wanted.y, 0.0, maxf(0.0, limit.y)),
	)


# One column, two actions. A BoxContainer rather than two children of the panel: a
# PanelContainer lays every child in the same rectangle, and two buttons that share a
# rectangle are one button whose label nobody can read. Stacked rather than side by
# side, because the panel belongs beside a character and a wide panel would cover the
# one on the other side of it.
func _build_actions() -> void:
	var row := VBoxContainer.new()
	row.name = "Actions"
	row.mouse_filter = Control.MOUSE_FILTER_STOP
	row.add_theme_constant_override("separation", 3)
	add_child(row)
	_button = _make_action("AttackButton", "Attack")
	_button.pressed.connect(_on_attack_pressed)
	row.add_child(_button)
	_recruit_button = _make_action("RecruitButton", "Recruit")
	_recruit_button.pressed.connect(_on_recruit_pressed)
	row.add_child(_recruit_button)
	# Closed until the click says the sheet carries the flag: a button that is only
	# hidden by the panel's own invisibility would be a Recruit waiting to be pressed
	# on a character who cannot be asked.
	_recruit_button.visible = false


func _make_action(button_name: String, label: String) -> Button:
	var made := Button.new()
	made.name = button_name
	made.text = label
	made.focus_mode = Control.FOCUS_NONE
	made.add_theme_font_size_override("font_size", 17)
	made.add_theme_color_override("font_color", INK)
	made.add_theme_color_override("font_hover_color", Color("fff0ce"))
	made.add_theme_color_override("font_pressed_color", GOLD)
	made.add_theme_stylebox_override("normal", _button_style(Color("382b21")))
	made.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
	made.add_theme_stylebox_override("pressed", _button_style(Color("241b16")))
	return made


func _button_style(background: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = BORDER_COLOR
	style.set_border_width_all(1)
	style.set_content_margin_all(4)
	return style
