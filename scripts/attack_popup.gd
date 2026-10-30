class_name AttackPopup
extends PanelContainer

# The one action a clicked character offers: Attack. This view holds no combat
# state and issues no order. It remembers only the id of the character that was
# clicked; confirming emits that id and closes, and the world asks C++ whether
# the target is still valid. Escape closes it, and a click anywhere outside it
# closes it too while still reaching world picking.

signal attack_requested(target_id: int)

const INK := Color("e0d0ae")
const GOLD := Color("d0a96b")
const PANEL_COLOR := Color("241b16")
const BORDER_COLOR := Color("806343")

# A nudge clear of the pointer, so the panel never opens under the click that
# summoned it.
const OPEN_OFFSET := Vector2(10.0, 6.0)

var _target_id := 0
var _button: Button


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
	_build_action()


func open_for_target(target_id: int, screen_position: Vector2) -> void:
	# An id that cannot name a character gets no panel: a visible Attack button that
	# can never order anything would be a lie about what is clickable.
	if target_id <= 0:
		close()
		return
	_target_id = target_id
	visible = true
	reset_size()
	position = _clamped_position(screen_position)


func close() -> void:
	_target_id = 0
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


# The panel belongs beside the click, but a click near an edge must not push its
# one button off screen.
func _clamped_position(screen_position: Vector2) -> Vector2:
	var wanted := screen_position + OPEN_OFFSET
	var limit := get_viewport_rect().size - size
	return Vector2(
		clampf(wanted.x, 0.0, maxf(0.0, limit.x)),
		clampf(wanted.y, 0.0, maxf(0.0, limit.y)),
	)


func _build_action() -> void:
	_button = Button.new()
	_button.name = "AttackButton"
	_button.text = "Attack"
	_button.focus_mode = Control.FOCUS_NONE
	_button.add_theme_font_size_override("font_size", 17)
	_button.add_theme_color_override("font_color", INK)
	_button.add_theme_color_override("font_hover_color", Color("fff0ce"))
	_button.add_theme_color_override("font_pressed_color", GOLD)
	_button.add_theme_stylebox_override("normal", _button_style(Color("382b21")))
	_button.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
	_button.add_theme_stylebox_override("pressed", _button_style(Color("241b16")))
	_button.pressed.connect(_on_attack_pressed)
	add_child(_button)


func _button_style(background: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = BORDER_COLOR
	style.set_border_width_all(1)
	style.set_content_margin_all(4)
	return style
