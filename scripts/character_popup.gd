class_name CharacterPopup
extends PanelContainer

# The I-key character screen: attributes on the left, grouped skills on the
# right. Every name and number comes from the C++ sheet through OpenCharacter;
# this view only draws them and keeps no gameplay state. Chosen
# specializations render beneath their basic skill; unchosen ones never appear,
# and resource pools are not shown.

# Opened from the sheet, because a character's techniques belong to the same screen it
# reads its skills from. The signal carries the position to open the panel at; what the
# player's technique settings are, and whether any of this is possible at all, stays C++'s.
signal techniques_requested(at: Vector2)

const ATTRIBUTE_NAMES := ["Strength", "Dexterity", "Agility", "Toughness", "Constitution",
	"Perception", "Intelligence", "Charisma", "Luck", "Profanity"]

# Snapshot rows arrive in BasicSkill order, which is the spec's table order, so
# each group is a contiguous index range. The group names are presentation;
# the skill names themselves come from the bridge, never from here.
const SKILL_GROUPS := [
	["Combat", 0, 6],
	["Physical", 7, 10],
	["Survival", 11, 14],
	["Crafting and labour", 15, 21],
	["Social", 22, 25],
	["Magic", 26, 31],
	["Utility", 32, 34],
]

const INK := Color("e0d0ae")
const GOLD := Color("d0a96b")
const MUTED_GOLD := Color("bd9862")
const PANEL_COLOR := Color("241b16")
const HEADER_COLOR := Color("493326")
const BORDER_COLOR := Color("806343")

var _sheet: OpenCharacter
var _title_label: Label
var _level_label: Label
var _serif_font: SystemFont
var _attributes_grid: GridContainer
var _attribute_value_labels: Array[Label] = []
var _skills_holder: VBoxContainer
var _skill_value_labels: Array[Label] = []
var _skill_signature := ""
var _position_initialized := false
var _dragging := false

# HorizontalAlignment enum values (Godot 4.7 does not expose the old global
# HORIZONTAL_ALIGNMENT_* names to GDScript).
const ALIGN_BEGIN := 0
const ALIGN_CENTER := 1
const ALIGN_END := 2


func _ready() -> void:
	custom_minimum_size = Vector2(760.0, 520.0)
	mouse_filter = Control.MOUSE_FILTER_STOP
	modulate.a = 0.9
	_serif_font = SystemFont.new()
	_serif_font.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	var panel_style := StyleBoxFlat.new()
	panel_style.bg_color = PANEL_COLOR
	panel_style.border_color = BORDER_COLOR
	panel_style.set_border_width_all(2)
	panel_style.set_content_margin_all(4)
	panel_style.shadow_color = Color(0.03, 0.02, 0.01, 0.6)
	panel_style.shadow_size = 12
	add_theme_stylebox_override("panel", panel_style)
	set_process(false)
	_build_skeleton()
	visibility_changed.connect(_on_visibility_changed)
	_clear_contents()


# The popup follows one sheet: values are re-read from it while it is open.
func bind_sheet(sheet: OpenCharacter) -> void:
	_sheet = sheet
	_refresh()


# Draw one snapshot array directly. While a sheet is bound and the popup is
# open, the live refresh replaces this on the next frame.
func render_snapshot(rows: Array) -> void:
	_rebuild_skills(rows)


func _process(_delta: float) -> void:
	# Center once, then leave the position under the user's control.
	if not _position_initialized:
		var viewport_size := get_viewport().get_visible_rect().size
		position = (viewport_size - size) / 2.0
		_position_initialized = true
	_refresh()


func _on_visibility_changed() -> void:
	set_process(visible)
	if not visible:
		_dragging = false
	if visible:
		_refresh()


func _on_techniques_pressed() -> void:
	# An entry point only: this opens a panel and sends no command. What the sheet's
	# technique settings are is read from C++, and only the technique popup's own controls
	# write to them.
	techniques_requested.emit(get_global_rect().end - Vector2(0.0, 40.0))


func _on_header_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		_dragging = event.pressed
	elif event is InputEventMouseMotion and _dragging:
		position += event.relative


func _refresh() -> void:
	if _sheet == null or not _sheet.is_configured():
		_clear_contents()
		return
	var sheet_name := String(_sheet.get_character_name())
	if sheet_name.is_empty():
		sheet_name = "Character"
	_title_label.text = sheet_name
	_level_label.text = "LEVEL %d" % _sheet.get_level()
	_render_attributes()
	_render_skills(_sheet.get_skill_snapshot())


func _build_skeleton() -> void:
	var column := VBoxContainer.new()
	column.name = "Column"
	column.add_theme_constant_override("separation", 0)
	add_child(column)

	var header_panel := PanelContainer.new()
	var header_style := StyleBoxFlat.new()
	header_style.bg_color = HEADER_COLOR
	header_style.border_color = BORDER_COLOR
	header_style.border_width_bottom = 1
	header_style.content_margin_left = 18
	header_style.content_margin_right = 18
	header_style.content_margin_top = 12
	header_style.content_margin_bottom = 12
	header_panel.add_theme_stylebox_override("panel", header_style)
	column.add_child(header_panel)

	var header := HBoxContainer.new()
	header.name = "Header"
	header.mouse_filter = Control.MOUSE_FILTER_STOP
	header.mouse_default_cursor_shape = Control.CURSOR_MOVE
	header.gui_input.connect(_on_header_gui_input)
	header.add_theme_constant_override("separation", 14)
	header_panel.add_child(header)
	var crest := _make_label("✦", 24, ALIGN_CENTER)
	crest.add_theme_color_override("font_color", GOLD)
	crest.custom_minimum_size = Vector2(36, 36)
	crest.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	header.add_child(crest)
	var identity := VBoxContainer.new()
	identity.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	identity.mouse_filter = Control.MOUSE_FILTER_IGNORE
	identity.add_theme_constant_override("separation", 1)
	header.add_child(identity)
	_title_label = _make_label("", 24)
	_title_label.add_theme_color_override("font_color", Color("f4e6c6"))
	identity.add_child(_title_label)
	_level_label = _make_label("", 11)
	_level_label.add_theme_color_override("font_color", MUTED_GOLD)
	identity.add_child(_level_label)
	var techniques := Button.new()
	techniques.name = "TechniquesButton"
	techniques.text = "Techniques"
	techniques.custom_minimum_size = Vector2(120, 34)
	techniques.add_theme_font_override("font", _serif_font)
	techniques.add_theme_font_size_override("font_size", 15)
	techniques.add_theme_color_override("font_color", INK)
	techniques.add_theme_color_override("font_hover_color", Color("fff0ce"))
	techniques.add_theme_stylebox_override("normal", _button_style(Color("382b21")))
	techniques.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
	techniques.add_theme_stylebox_override("pressed", _button_style(Color("241b16")))
	techniques.pressed.connect(_on_techniques_pressed)
	header.add_child(techniques)

	var close := Button.new()
	close.name = "CloseButton"
	close.text = "×"
	close.custom_minimum_size = Vector2(34, 34)
	close.add_theme_font_override("font", _serif_font)
	close.add_theme_font_size_override("font_size", 23)
	close.add_theme_color_override("font_color", INK)
	close.add_theme_color_override("font_hover_color", Color("fff0ce"))
	close.add_theme_stylebox_override("normal", _button_style(Color("382b21")))
	close.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
	close.add_theme_stylebox_override("pressed", _button_style(Color("241b16")))
	close.pressed.connect(hide)
	header.add_child(close)

	var body_margin := MarginContainer.new()
	body_margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body_margin.add_theme_constant_override("margin_left", 24)
	body_margin.add_theme_constant_override("margin_right", 24)
	body_margin.add_theme_constant_override("margin_top", 22)
	body_margin.add_theme_constant_override("margin_bottom", 18)
	column.add_child(body_margin)
	var body := HBoxContainer.new()
	body.name = "Body"
	body.size_flags_vertical = Control.SIZE_EXPAND_FILL
	body.add_theme_constant_override("separation", 24)
	body_margin.add_child(body)

	var left := VBoxContainer.new()
	left.name = "Attributes"
	left.custom_minimum_size = Vector2(260.0, 0.0)
	left.add_theme_constant_override("separation", 12)
	body.add_child(left)
	left.add_child(_section_heading("ATTRIBUTES"))
	_attributes_grid = GridContainer.new()
	_attributes_grid.name = "AttributesGrid"
	_attributes_grid.columns = 2
	_attributes_grid.add_theme_constant_override("h_separation", 24)
	_attributes_grid.add_theme_constant_override("v_separation", 8)
	left.add_child(_attributes_grid)

	var divider := VSeparator.new()
	var divider_style := StyleBoxLine.new()
	divider_style.color = Color("675038")
	divider_style.vertical = true
	divider.add_theme_stylebox_override("separator", divider_style)
	body.add_child(divider)

	var right := VBoxContainer.new()
	right.name = "Skills"
	right.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	right.add_theme_constant_override("separation", 12)
	body.add_child(right)
	right.add_child(_section_heading("SKILLS"))
	var scroller := ScrollContainer.new()
	scroller.name = "SkillsScroll"
	scroller.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroller.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	right.add_child(scroller)
	var scrollbar := scroller.get_v_scroll_bar()
	scrollbar.custom_minimum_size.x = 8
	scrollbar.add_theme_stylebox_override("scroll", _scrollbar_style(Color("2c211a")))
	scrollbar.add_theme_stylebox_override("grabber", _scrollbar_style(Color("8c6b45")))
	scrollbar.add_theme_stylebox_override("grabber_highlight", _scrollbar_style(Color("b08a58")))
	scrollbar.add_theme_stylebox_override("grabber_pressed", _scrollbar_style(Color("d0a96b")))
	_skills_holder = VBoxContainer.new()
	_skills_holder.name = "SkillsHolder"
	_skills_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_skills_holder.add_theme_constant_override("separation", 8)
	scroller.add_child(_skills_holder)

	var footer := _make_label("DRAG HEADER TO MOVE     ·     I TO CLOSE", 11)
	footer.add_theme_color_override("font_color", Color("9c886d"))
	var footer_margin := MarginContainer.new()
	footer_margin.add_theme_constant_override("margin_left", 24)
	footer_margin.add_theme_constant_override("margin_bottom", 12)
	footer_margin.add_child(footer)
	column.add_child(footer_margin)


func _clear_contents() -> void:
	_title_label.text = ""
	_level_label.text = ""
	for child in _attributes_grid.get_children():
		child.free()
	_attribute_value_labels.clear()
	for child in _skills_holder.get_children():
		child.free()
	_skill_value_labels.clear()
	_skill_signature = ""


func _render_attributes() -> void:
	if _attribute_value_labels.is_empty():
		for attribute_name in ATTRIBUTE_NAMES:
			var name_label := _make_label(String(attribute_name))
			name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
			_attributes_grid.add_child(name_label)
			var value_label := _make_label("", 16, ALIGN_END)
			value_label.add_theme_color_override("font_color", Color("f0d3a0"))
			_attributes_grid.add_child(value_label)
			_attribute_value_labels.append(value_label)
	for i in ATTRIBUTE_NAMES.size():
		_attribute_value_labels[i].text = _format_value(_sheet.get_attribute(i))


func _render_skills(rows: Array) -> void:
	var signature := ""
	for row in rows:
		signature += String(row["name"]) + "|" + String(row["advanced_name"]) + ";"
	if signature == _skill_signature:
		var index := 0
		for row in rows:
			_skill_value_labels[index].text = _format_value(float(row["value"]))
			index += 1
			if String(row["advanced_name"]) != "":
				_skill_value_labels[index].text = _format_value(float(row["advanced_value"]))
				index += 1
		return
	_rebuild_skills(rows)


func _rebuild_skills(rows: Array) -> void:
	for child in _skills_holder.get_children():
		child.free()
	_skill_value_labels.clear()
	_skill_signature = ""
	if rows.is_empty():
		return
	var grouped := rows.size() == 35
	if grouped:
		for group in SKILL_GROUPS:
			var heading := _make_label(String(group[0]).to_upper(), 13)
			heading.add_theme_color_override("font_color", MUTED_GOLD)
			_skills_holder.add_child(heading)
			var grid := _add_skill_grid()
			for i in range(int(group[1]), int(group[2]) + 1):
				_add_skill_row(grid, rows[i])
		_signature_from(rows)
	else:
		var grid := _add_skill_grid()
		for row in rows:
			_add_skill_row(grid, row)
		_signature_from(rows)


func _add_skill_grid() -> GridContainer:
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 18)
	grid.add_theme_constant_override("v_separation", 7)
	grid.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_skills_holder.add_child(grid)
	return grid


func _add_skill_row(grid: GridContainer, row: Dictionary) -> void:
	_add_row(grid, String(row["name"]), float(row["value"]))
	var advanced_name := String(row["advanced_name"])
	if advanced_name != "":
		_add_row(grid, advanced_name, float(row["advanced_value"]))


func _add_row(grid: GridContainer, name_text: String, value: float) -> void:
	var name_label := _make_label(name_text)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	grid.add_child(name_label)
	var value_label := _make_label(_format_value(value), 16, ALIGN_END)
	value_label.add_theme_color_override("font_color", Color("f0d3a0"))
	grid.add_child(value_label)
	_skill_value_labels.append(value_label)


func _signature_from(rows: Array) -> void:
	_skill_signature = ""
	for row in rows:
		_skill_signature += String(row["name"]) + "|" + String(row["advanced_name"]) + ";"


func _make_label(text: String, font_size: int = 16, alignment: int = ALIGN_BEGIN) -> Label:
	var label := Label.new()
	label.text = text
	label.horizontal_alignment = alignment
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_override("font", _serif_font)
	label.add_theme_color_override("font_color", INK)
	label.add_theme_font_size_override("font_size", font_size)
	return label


func _button_style(background: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = BORDER_COLOR
	style.set_border_width_all(1)
	style.set_content_margin_all(3)
	return style


func _scrollbar_style(color: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = color
	style.set_corner_radius_all(2)
	return style


func _section_heading(text: String) -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 12)
	var label := _make_label(text, 17)
	label.add_theme_color_override("font_color", Color("e3c590"))
	row.add_child(label)
	var line := HSeparator.new()
	line.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	line.size_flags_vertical = Control.SIZE_SHRINK_CENTER
	var line_style := StyleBoxLine.new()
	line_style.color = Color("6e5539")
	line.add_theme_stylebox_override("separator", line_style)
	row.add_child(line)
	return row


func _format_value(value: float) -> String:
	var text := String.num(value, 3)
	while text.ends_with("0") and not text.ends_with(".0"):
		text = text.substr(0, text.length() - 1)
	return text
