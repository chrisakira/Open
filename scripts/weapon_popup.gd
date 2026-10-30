class_name WeaponPopup
extends PanelContainer

# Presentation-only hook: the name identifies a display model, not an item in
# the simulation. Nothing is connected to this signal until equipment is defined.
signal equip_requested(model_name: String)

const OPEN_OFFSET := Vector2(10.0, 6.0)

var _model_name := ""


func _ready() -> void:
	custom_minimum_size = Vector2(150.0, 34.0)
	mouse_filter = Control.MOUSE_FILTER_STOP
	modulate.a = 0.94
	visible = false
	var panel := StyleBoxFlat.new()
	panel.bg_color = Color("241b16")
	panel.border_color = Color("806343")
	panel.set_border_width_all(2)
	panel.set_corner_radius_all(3)
	panel.set_content_margin_all(5)
	panel.shadow_color = Color(0.03, 0.02, 0.01, 0.6)
	panel.shadow_size = 8
	add_theme_stylebox_override("panel", panel)
	var button := Button.new()
	button.name = "EquipButton"
	button.text = "Equip (coming soon)"
	button.focus_mode = Control.FOCUS_NONE
	button.add_theme_font_size_override("font_size", 17)
	button.add_theme_color_override("font_color", Color("e0d0ae"))
	button.pressed.connect(_on_equip_pressed)
	add_child(button)


func open_for_model(model_name: String, screen_position: Vector2) -> void:
	_model_name = model_name
	visible = true
	reset_size()
	var wanted := screen_position + OPEN_OFFSET
	var limit := get_viewport_rect().size - size
	position = Vector2(clampf(wanted.x, 0.0, maxf(0.0, limit.x)),
		clampf(wanted.y, 0.0, maxf(0.0, limit.y)))


func close() -> void:
	_model_name = ""
	visible = false


func covers(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func _unhandled_key_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and not event.echo \
			and (event.keycode == KEY_ESCAPE or event.physical_keycode == KEY_ESCAPE):
		close()
		get_viewport().set_input_as_handled()


func _on_equip_pressed() -> void:
	if not visible or _model_name.is_empty():
		return
	var name := _model_name
	close()
	equip_requested.emit(name)
