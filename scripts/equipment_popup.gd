class_name EquipmentPopup
extends PanelContainer

# The character sheet's Equipment screen: what this sheet owns, what it is wearing, and what
# holding a thing would be worth.
#
# Presentation and a request, nothing more. Every row comes from the sheet's equipment
# snapshot, and the numbers beside a weapon - effectiveness, the cost an attempt carries, reach,
# the techniques it offers - come from the same C++ resolution a swing uses, so the panel cannot
# promise a weapon is worth more than the blow turns out to be. A click emits a request and the
# world carries it to the sheet; the panel then shows what C++ agreed to, not what it hoped for.
#
# Two rules from docs/weapons.md shape the controls. The player chooses the hand: a one-handed
# item is offered in the left and the right separately, and a two-handed one takes both. And a
# hand that is busy is never solved silently: the button is not offered, and the row names what
# is standing in it, because "unequip that first" is the player's decision to make.

const OPEN_SIZE := Vector2(560.0, 470.0)
const OPEN_OFFSET := Vector2(10.0, 6.0)
const SLOT_LEFT_HAND := 5
const SLOT_RIGHT_HAND := 6
const HAND_USE_TWO_HANDED := 2

const INK := Color("e0d0ae")
const FAINT := Color("9c886d")
const MUTED_GOLD := Color("bd9862")
const PANEL_COLOR := Color("241b16")
const HEADER_COLOR := Color("493326")
const BORDER_COLOR := Color("806343")
const ROW_COLOR := Color("382b21")
# The attribute order is the simulation's own enum order, so the name comes from the id and
# nothing is guessed.
const ATTRIBUTE_NAMES := ["Strength", "Dexterity", "Agility", "Toughness", "Constitution",
	"Perception", "Intelligence", "Charisma", "Luck", "Profanity"]

const SLOT_NAMES := {
	SLOT_LEFT_HAND: "Left hand",
	SLOT_RIGHT_HAND: "Right hand",
}

signal equip_requested(item_id: int, slot: int)
signal unequip_requested(slot: int)

var _sheet  # anything that answers get_equipment_snapshot(), get_weapon_snapshot(id) and reads
var _serif_font: SystemFont
var _title_label: Label
var _status_label: Label
var _rows_holder: VBoxContainer
var _signature := ""
var _message := ""
var _rows_cache: Array = []
var _position_initialized := false
var _opening_position := Vector2.ZERO


func _ready() -> void:
	custom_minimum_size = OPEN_SIZE
	size = OPEN_SIZE
	mouse_filter = Control.MOUSE_FILTER_STOP
	modulate.a = 0.92
	visible = false
	_serif_font = SystemFont.new()
	_serif_font.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	var panel := StyleBoxFlat.new()
	panel.bg_color = PANEL_COLOR
	panel.border_color = BORDER_COLOR
	panel.set_border_width_all(2)
	panel.set_corner_radius_all(3)
	panel.set_content_margin_all(4)
	panel.shadow_color = Color(0.03, 0.02, 0.01, 0.6)
	panel.shadow_size = 10
	add_theme_stylebox_override("panel", panel)
	set_process(false)
	_build_skeleton()
	visibility_changed.connect(_on_visibility_changed)
	_render([])


func bind_sheet(sheet) -> void:
	_sheet = sheet
	_message = ""
	# Every bind redraws from scratch: the panel is opening on a sheet, and last session's
	# status line - refusal or default - is not this one's first impression.
	_signature = ""
	_refresh()


func open_for_sheet(sheet, screen_position: Vector2) -> void:
	bind_sheet(sheet)
	_position_initialized = false
	_opening_position = screen_position
	visible = true


func close() -> void:
	visible = false


func covers(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func _process(_delta: float) -> void:
	if not _position_initialized:
		_place()
		_position_initialized = true
	_refresh()


func _on_visibility_changed() -> void:
	set_process(visible)
	if visible:
		_refresh()


func _unhandled_key_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and not event.echo \
			and (event.keycode == KEY_ESCAPE or event.physical_keycode == KEY_ESCAPE):
		close()
		get_viewport().set_input_as_handled()


func _place() -> void:
	var viewport_size := get_viewport_rect().size
	var wanted := _opening_position + OPEN_OFFSET
	var limit := viewport_size - size
	position = Vector2(clampf(wanted.x, 0.0, maxf(0.0, limit.x)),
		clampf(wanted.y, 0.0, maxf(0.0, limit.y)))


func _refresh() -> void:
	if _sheet == null or not _sheet.is_configured():
		_render([])
		return
	_render(_sheet.get_equipment_snapshot())


# --- requests -----------------------------------------------------------------

func _request_equip(item_id: int, slot: int) -> void:
	_message = ""
	equip_requested.emit(item_id, slot)
	_rebuild_next_frame()


func _request_unequip(slot: int) -> void:
	_message = ""
	unequip_requested.emit(slot)
	_rebuild_next_frame()


# Asked for, not assumed: the rows are redrawn from what the sheet says next frame, so a
# refusal from C++ - a busy hand, a limb that cannot hold it, an id that belongs to somebody
# else - shows up as the row still being carried. Rebuilding here instead would free the very
# button whose own `pressed` signal is still running, which is the crash Godot warns about.


# --- drawing ------------------------------------------------------------------

func _owned_slots(rows: Array) -> Dictionary:
	# slot -> the model name standing in it, which is all the panel needs to explain a hand
	# that is not free. The state is C++'s; this only reads it back.
	var held := {}
	for row in rows:
		for slot in row["slots"]:
			held[int(slot)] = String(row["model_key"])
	return held


func _rebuild_next_frame() -> void:
	_signature = ""


func _render(rows: Array) -> void:
	_rows_cache = rows
	var signature := str(rows.size())
	for row in rows:
		signature += "|%d:%s:%d" % [int(row["item_id"]), str(row["slots"]), int(row["durability"])]
	if signature == _signature:
		# The sheet says the same thing this panel already drew: the controls stay standing
		# under the pointer, and a refusal's message keeps its turn on the status line.
		return
	_signature = signature
	for child in _rows_holder.get_children():
		child.free()
	_title_label.text = "Equipment"
	if rows.is_empty():
		_message = ""
		_status_label.text = "Nothing owned yet."
		return

	var held := _owned_slots(rows)
	for row in rows:
		# The rows are drawn first because a busy hand explains itself as it is drawn: whatever
		# they had to say is the status line for this frame.
		_build_row(row, held)
	if _message.is_empty():
		_message = "Choose the hand. A busy hand is never swapped automatically."
	_status_label.text = _message


func _build_row(row: Dictionary, held: Dictionary) -> void:
	var item_id := int(row["item_id"])
	var model_key := String(row["model_key"])
	var container := VBoxContainer.new()
	container.name = "Row%d" % item_id
	container.add_theme_constant_override("separation", 2)
	container.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows_holder.add_child(container)

	var top := HBoxContainer.new()
	top.add_theme_constant_override("separation", 8)
	container.add_child(top)

	var slots: Array = row["slots"]
	var where := "Carried"
	if slots.size() > 0:
		var names := []
		for slot in slots:
			names.append(String(SLOT_NAMES.get(int(slot), "Slot %d" % int(slot))))
		where = ", ".join(names)
	var name_label := _label("%s  ·  quality %d  ·  %d/%d  ·  %s" % [model_key,
		int(row["quality"]), int(row["durability"]), int(row["max_durability"]), where], 15)
	name_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	top.add_child(name_label)

	var weapon: Dictionary = {}
	if _sheet != null:
		weapon = _sheet.get_weapon_snapshot(item_id)
	var worth := ""
	if not weapon.is_empty():
		# The numbers a swing would actually use, from the same resolution a swing uses.
		worth = "held at %s  ·  %s per swing" % [_percent(float(weapon["effectiveness"])),
			_number(float(weapon["attack_cost"]))]
	elif String(model_key) == "Shield_Round":
		worth = "a shield: it defends, it never attacks"
	if worth.is_empty():
		worth = String(row["model_key"])
	top.add_child(_label(worth, 12, 2, FAINT))

	var controls := HBoxContainer.new()
	controls.add_theme_constant_override("separation", 6)
	container.add_child(controls)
	var two_handed := int(row["hand_use"]) == HAND_USE_TWO_HANDED

	if slots.size() > 0:
		var unequip := _button("Unequip%d" % item_id, "Unequip")
		unequip.pressed.connect(_request_unequip.bind(int(slots[0])))
		controls.add_child(unequip)
	else:
		# A two-hander is one decision - both hands - and starts from the hand C++ keeps it in.
		_add_hand_button(controls, item_id, SLOT_LEFT_HAND, "Both hands" if two_handed else "Left",
			held, two_handed)
		if not two_handed:
			_add_hand_button(controls, item_id, SLOT_RIGHT_HAND, "Right", held, false)

	# What the item asks of this wielder, with what the wielder has: an unmet requirement is
	# shown, not hidden, because equipping it anyway is legal and the penalty is real.
	var requirements: Array = row["requirements"]
	if requirements.size() > 0 and _sheet != null:
		var parts := []
		for requirement in requirements:
			var attribute := int(requirement["attribute"])
			parts.append("%s %s (this sheet %s)" % [_attribute_name(attribute),
				_number(float(requirement["value"])), _number(_sheet.get_attribute(attribute))])
		container.add_child(_label("Asks: " + ", ".join(parts), 12, 0, FAINT))
	var enchantments: Array = row["enchantments"]
	if enchantments.size() > 0:
		container.add_child(_label("Enchanted: %d of %d slots used"
				% [enchantments.size(), int(row["enchantment_capacity"])], 12, 0, FAINT))


func _add_hand_button(controls: HBoxContainer, item_id: int, slot: int, text: String,
		held: Dictionary, two_handed: bool) -> void:
	var button := _button("Equip%d_%d" % [item_id, slot], text)
	var blockers := []
	if held.has(slot):
		blockers.append(String(SLOT_NAMES.get(slot, "hand")))
	if two_handed:
		for other in [SLOT_LEFT_HAND, SLOT_RIGHT_HAND]:
			if held.has(other):
				blockers.append(String(SLOT_NAMES.get(other, "hand")))
	if blockers.is_empty():
		button.pressed.connect(_request_equip.bind(item_id, slot))
		controls.add_child(button)
		return
	# Not offered, and the row says exactly why and whose it is: the way to free a hand is the
	# player's choice, and never something the panel does on their behalf.
	button.disabled = true
	controls.add_child(button)
	_message = "%s is held in the %s - unequip it first." % [
		", ".join(blockers.map(func(name: String) -> String: return _who_holds(held, name))),
		" and ".join(blockers)]


func _who_holds(held: Dictionary, slot_name: String) -> String:
	for slot in held.keys():
		if String(SLOT_NAMES.get(int(slot), "")) == slot_name:
			return String(held[slot])
	return "an item"


func _attribute_name(attribute: int) -> String:
	if attribute >= 0 and attribute < ATTRIBUTE_NAMES.size():
		return ATTRIBUTE_NAMES[attribute]
	return "Attribute %d" % attribute


func _percent(value: float) -> String:
	return "%s%%" % _number(value * 100.0)


func _number(value: float) -> String:
	# Shortest honest form: 5 asks for "5", 0.04 of effectiveness for "4%", and 15.68 for what
	# it is. No rounding past the two digits the numbers are specified to.
	var text := String.num(value, 2)
	while text.ends_with("0"):
		text = text.substr(0, text.length() - 1)
	if text.ends_with("."):
		text = text.substr(0, text.length() - 1)
	return text


# --- skeleton and controls ----------------------------------------------------

func _build_skeleton() -> void:
	var column := VBoxContainer.new()
	column.name = "Column"
	column.add_theme_constant_override("separation", 6)
	add_child(column)

	var header_panel := PanelContainer.new()
	var header_style := StyleBoxFlat.new()
	header_style.bg_color = HEADER_COLOR
	header_style.border_color = BORDER_COLOR
	header_style.border_width_bottom = 1
	header_style.set_content_margin_all(8)
	header_panel.add_theme_stylebox_override("panel", header_style)
	column.add_child(header_panel)
	var header := HBoxContainer.new()
	header.name = "Header"
	header.mouse_filter = Control.MOUSE_FILTER_STOP
	header.add_theme_constant_override("separation", 10)
	header_panel.add_child(header)
	_title_label = _label("Equipment", 20)
	_title_label.add_theme_color_override("font_color", Color("f4e6c6"))
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_title_label)
	var close_button := Button.new()
	close_button.name = "CloseButton"
	close_button.text = "×"
	close_button.custom_minimum_size = Vector2(28, 26)
	_style_control(close_button)
	close_button.pressed.connect(_on_close_pressed)
	header.add_child(close_button)

	_status_label = _label("", 12)
	_status_label.add_theme_color_override("font_color", FAINT)
	_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status_label.custom_minimum_size = Vector2(520.0, 0.0)
	column.add_child(_status_label)

	column.add_child(_label("OWNED ITEMS", 13, 0, MUTED_GOLD))
	var scroller := ScrollContainer.new()
	scroller.name = "ItemsScroll"
	scroller.custom_minimum_size = Vector2(0.0, 260.0)
	scroller.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroller.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_rows_holder = VBoxContainer.new()
	_rows_holder.name = "ItemsHolder"
	_rows_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows_holder.add_theme_constant_override("separation", 8)
	scroller.add_child(_rows_holder)
	column.add_child(scroller)

	column.add_child(_label("Clicks only change what this sheet holds.", 11, 0, FAINT))


func _on_close_pressed() -> void:
	close()


func _button(button_name: String, text: String) -> Button:
	var button := Button.new()
	button.name = button_name
	button.text = text
	button.custom_minimum_size = Vector2(74, 0)
	_style_control(button)
	return button


func _style_control(button: Button) -> void:
	button.focus_mode = Control.FOCUS_NONE
	button.add_theme_font_override("font", _serif_font)
	button.add_theme_font_size_override("font_size", 13)
	button.add_theme_color_override("font_color", INK)
	button.add_theme_color_override("font_hover_color", Color("fff0ce"))
	button.add_theme_color_override("font_disabled_color", FAINT)
	button.add_theme_stylebox_override("normal", _button_style(ROW_COLOR))
	button.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
	button.add_theme_stylebox_override("pressed", _button_style(PANEL_COLOR))
	button.add_theme_stylebox_override("disabled", _button_style(Color("2a211a")))


func _label(text: String, font_size: int = 15, alignment: int = 0,
		color: Color = INK) -> Label:
	var label := Label.new()
	label.text = text
	label.horizontal_alignment = alignment
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_override("font", _serif_font)
	label.add_theme_color_override("font_color", color)
	label.add_theme_font_size_override("font_size", font_size)
	return label


func _button_style(background: Color) -> StyleBoxFlat:
	var style := StyleBoxFlat.new()
	style.bg_color = background
	style.border_color = BORDER_COLOR
	style.set_border_width_all(1)
	style.set_content_margin_all(3)
	return style
