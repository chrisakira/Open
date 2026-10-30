class_name TechniquePopup
extends PanelContainer

# The technique screen one sheet owns: what the weapon in its hand offers, what the sheet has
# configured, and what it still owes.
#
# Presentation only. Every number on this panel comes from the sheet's technique snapshot,
# which C++ builds from the sheet's own state, the definition table and the attack pipeline's
# own eligibility rule - so a row this popup greys out is a move the swing would refuse, and
# a countdown it shows is the clock the swing started. Nothing is decided here: a click turns
# into exactly one `configure_techniques` command, the view is then whatever C++ agrees to,
# and a command it refuses leaves the panel showing what it showed before.
#
# Opening is a read and nothing else. No click that is not on one of these controls can order
# a move, an attack or an edit, and no clock is reset by looking at the state it is running.

const OPEN_SIZE := Vector2(430.0, 470.0)
const OPEN_OFFSET := Vector2(10.0, 6.0)

const INK := Color("e0d0ae")
const GOLD := Color("d0a96b")
const MUTED_GOLD := Color("bd9862")
const FAINT := Color("9c886d")
const PANEL_COLOR := Color("241b16")
const HEADER_COLOR := Color("493326")
const BORDER_COLOR := Color("806343")
const ROW_COLOR := Color("382b21")

var _sheet  # any sheet that answers the three messages below: usually an OpenCharacter
var _serif_font: SystemFont
var _title_label: Label
var _status_label: Label
var _enable_button: Button
var _available_holder: VBoxContainer
var _sequence_holder: VBoxContainer
var _available_times: Array[Label] = []
var _sequence_times: Array[Label] = []
var _signature := ""
var _snapshot: Dictionary = {}
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
	_render({})


# The panel follows one sheet, re-reading it while it is open so a cooldown a swing started
# comes down on its own and an entry the weapon cannot offer says so the moment the weapon
# changes.
func bind_sheet(sheet) -> void:
	_sheet = sheet
	_position_initialized = false
	_refresh()


# Draw one snapshot dictionary directly: the same path the live sheet drives, for a component
# check that has no sheet in front of it.
func render_snapshot(snapshot: Dictionary) -> void:
	_render(snapshot)


func open_for_sheet(sheet, screen_position: Vector2) -> void:
	bind_sheet(sheet)
	_position_initialized = false
	_opening_position = screen_position
	visible = true
	_place()


func close() -> void:
	visible = false


func _on_close_pressed() -> void:
	close()


func covers(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func _process(_delta: float) -> void:
	# Placed once, on the frame it opens, when the panel knows how big it came out.
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
		_render({})
		return
	_render(_sheet.get_technique_snapshot())


# --- the one command ---------------------------------------------------------

func _steps_of(snapshot: Dictionary) -> Array:
	var steps := []
	for entry in snapshot.get("sequence", []):
		steps.append({"technique_id": int(entry["technique_id"]),
			"skip_on_cooldown": bool(entry["skip_on_cooldown"])})
	return steps


func _send(enabled: bool, steps: Array) -> void:
	if _sheet == null or not _sheet.is_configured():
		return
	# The whole sequence in one command, and the view rebuilt from what C++ kept: a refusal
	# leaves the previous sequence on the panel because it left it on the sheet.
	_sheet.configure_techniques(enabled, steps)
	# Rebuilt on the next refresh, not here: the button being pressed is one of the rows this
	# panel is about to replace, and freeing a node while its own signal is running is the
	# crash Godot warns about. One frame later the panel shows the same fact it would have.
	_signature = ""


func _toggle_enabled() -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	_send(not bool(_snapshot.get("enabled", true)), _steps_of(_snapshot))


func _add(technique_id: int) -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	var steps := _steps_of(_snapshot)
	steps.append({"technique_id": technique_id, "skip_on_cooldown": false})
	_send(bool(_snapshot.get("enabled", true)), steps)


func _remove(at: int) -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	var steps := _steps_of(_snapshot)
	if at < 0 or at >= steps.size():
		return
	steps.remove_at(at)
	_send(bool(_snapshot.get("enabled", true)), steps)


func _duplicate(at: int) -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	var steps := _steps_of(_snapshot)
	if at < 0 or at >= steps.size():
		return
	steps.insert(at + 1, steps[at])
	_send(bool(_snapshot.get("enabled", true)), steps)


func _move(from: int, offset: int) -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	var steps := _steps_of(_snapshot)
	var to := from + offset
	if from < 0 or from >= steps.size() or to < 0 or to >= steps.size():
		return
	var moved = steps[from]
	steps.remove_at(from)
	steps.insert(to, moved)
	_send(bool(_snapshot.get("enabled", true)), steps)


func _toggle_skip(at: int) -> void:
	if not bool(_snapshot.get("can_configure", false)):
		return
	var steps := _steps_of(_snapshot)
	if at < 0 or at >= steps.size():
		return
	steps[at]["skip_on_cooldown"] = not bool(steps[at]["skip_on_cooldown"])
	_send(bool(_snapshot.get("enabled", true)), steps)


# --- drawing -----------------------------------------------------------------

func _render(snapshot: Dictionary) -> void:
	_snapshot = snapshot
	var configured := not snapshot.is_empty()
	_enable_button.disabled = not configured or not bool(snapshot.get("can_configure", false))

	if not configured:
		_clear_rows()
		_title_label.text = "Techniques"
		_enable_button.text = "Techniques: -"
		_status_label.text = "No sheet to read."
		_signature = ""
		return

	_title_label.text = "Techniques"
	_enable_button.text = "Techniques: %s" % ("On" if bool(snapshot.get("enabled", true)) else "Off")
	var supported: Array = snapshot.get("supported", [])
	var sequence: Array = snapshot.get("sequence", [])
	if supported.is_empty():
		_status_label.text = "Nothing held offers a technique. Bare hands have none to learn."
	elif sequence.is_empty():
		_status_label.text = "Add a technique, or this sheet attacks on its own."
	else:
		_status_label.text = "Each change is saved as it is made. The sequence restarts at its first entry."

	# `can_configure` is part of the structure, not a number to refresh: which controls are
	# live changes what is on the panel, not just what they say.
	var signature := str(snapshot.get("enabled")) + "|" + str(snapshot.get("can_configure"))
	signature += "|" + str(snapshot.get("index")) + "|"
	for entry in supported:
		signature += "%s:%s," % [str(entry["technique_id"]), str(entry["available"])]
	signature += ";"
	for entry in sequence:
		signature += "%s:%s:%s," % [str(entry["technique_id"]), str(entry["available"]),
			str(entry["skip_on_cooldown"])]
	if signature == _signature:
		# Same structure, so only the numbers move: refresh them in place rather than
		# rebuilding every control under a frame that just wants a clock redrawn.
		_write_times(supported, sequence)
		return
	_signature = signature
	_clear_rows()
	_build_available(supported)
	_build_sequence(sequence, int(snapshot.get("index", 0)))
	_write_times(supported, sequence)


# The clear belongs with the rebuild: wiping the rows and then deciding the structure had not
# changed would leave the panel showing nothing at all.
func _clear_rows() -> void:
	for holder in [_available_holder, _sequence_holder]:
		for child in holder.get_children():
			child.free()
	_available_times.clear()
	_sequence_times.clear()


func _build_available(supported: Array) -> void:
	if supported.is_empty():
		_available_holder.add_child(_note("No technique is offered by what this sheet holds."))
		return
	for entry in supported:
		var row := _row()
		var technique_id := int(entry["technique_id"])
		var label := _wrapped(_entry_text(entry), 15)
		row.add_child(label)
		row.add_child(_make_time_label(_available_times))
		var add := Button.new()
		add.name = "Add%d" % technique_id
		add.focus_mode = Control.FOCUS_NONE
		add.add_theme_font_override("font", _serif_font)
		add.add_theme_font_size_override("font_size", 13)
		add.add_theme_color_override("font_color", INK)
		add.add_theme_stylebox_override("normal", _button_style(ROW_COLOR))
		add.add_theme_stylebox_override("hover", _button_style(Color("62472f")))
		add.add_theme_stylebox_override("disabled", _button_style(Color("2a211a")))
		add.add_theme_color_override("font_disabled_color", FAINT)
		add.pressed.connect(_add.bind(technique_id))
		if bool(entry["available"]):
			add.text = "Add"
			add.disabled = not bool(_snapshot.get("can_configure", false))
		elif bool(entry["defined"]):
			# Unavailable, and the sheet says why: the rank this swing resolves to against the
			# rank the technique asks for. The rule itself, including the title route, stays in
			# C++ - `available` is its answer, and this is only its data.
			add.text = "Rank %d (this swing %d)" % [int(entry["required_rank"]), int(entry["trained"])]
			add.disabled = true
		else:
			add.text = "Unavailable"
			add.disabled = true
		row.add_child(add)
		_available_holder.add_child(row)


func _build_sequence(sequence: Array, index: int) -> void:
	if sequence.is_empty():
		_sequence_holder.add_child(_note("Nothing configured: this sheet chooses on its own."))
		return
	for at in sequence.size():
		var entry = sequence[at]
		var row := _row()
		# The marker is the index C++ holds: which entry the next attack owes.
		var marker := "▸ " if at == index else "   "
		var label := _wrapped("%s%d. %s" % [marker, at + 1, _entry_text(entry)], 15)
		# Kept, and shown for what it is: an entry the held weapon does not offer is not
		# deleted, because the next weapon may offer it and the clock on it is still running.
		if not bool(entry["available"]):
			label.add_theme_color_override("font_color", FAINT)
		row.add_child(label)
		row.add_child(_make_time_label(_sequence_times))
		var editable := bool(_snapshot.get("can_configure", false))
		_row_button(row, "Up%d" % at, "↑", _move.bind(at, -1), editable)
		_row_button(row, "Down%d" % at, "↓", _move.bind(at, 1), editable)
		_row_button(row, "Copy%d" % at, "⧉", _duplicate.bind(at), editable)
		var skip := Button.new()
		skip.name = "Skip%d" % at
		skip.text = "Wait" if bool(entry["skip_on_cooldown"]) else "Skip"
		_style_control(skip)
		skip.disabled = not editable
		skip.pressed.connect(_toggle_skip.bind(at))
		row.add_child(skip)
		_row_button(row, "Remove%d" % at, "×", _remove.bind(at), editable)
		_sequence_holder.add_child(row)


func _entry_text(entry: Dictionary) -> String:
	var mode := "Skip" if bool(entry["skip_on_cooldown"]) else "Wait"
	var text := "Technique %d · %s" % [int(entry["technique_id"]), mode]
	if not bool(entry["available"]):
		text += " — not offered now"
	return text


func _write_times(supported: Array, sequence: Array) -> void:
	for i in min(_available_times.size(), supported.size()):
		_available_times[i].text = _remaining(supported[i])
	for i in min(_sequence_times.size(), sequence.size()):
		_sequence_times[i].text = _remaining(sequence[i])


func _remaining(entry: Dictionary) -> String:
	var left := float(entry["cooldown_remaining"])
	if left <= 0.0:
		return "ready"
	return "%.1fs" % left


# --- skeleton ----------------------------------------------------------------

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
	_title_label = _make_label("Techniques", 20)
	_title_label.add_theme_color_override("font_color", Color("f4e6c6"))
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_title_label)
	_enable_button = Button.new()
	_enable_button.name = "EnableButton"
	_enable_button.text = "Techniques: -"
	_style_control(_enable_button)
	_enable_button.pressed.connect(_toggle_enabled)
	header.add_child(_enable_button)
	var close_button := Button.new()
	close_button.name = "CloseButton"
	close_button.text = "×"
	close_button.custom_minimum_size = Vector2(28, 26)
	_style_control(close_button)
	close_button.pressed.connect(_on_close_pressed)
	header.add_child(close_button)

	_status_label = _make_label("", 12)
	_status_label.add_theme_color_override("font_color", FAINT)
	_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status_label.custom_minimum_size = Vector2(400.0, 0.0)
	column.add_child(_status_label)

	column.add_child(_section_heading("OFFERED BY WHAT YOU HOLD"))
	_available_holder = VBoxContainer.new()
	column.add_child(_add_scroller("AvailableScroll", _available_holder))
	column.add_child(_section_heading("SEQUENCE"))
	_sequence_holder = VBoxContainer.new()
	column.add_child(_add_scroller("SequenceScroll", _sequence_holder))

	var footer := _make_label("Clicks only change this sheet's technique settings.", 11)
	footer.add_theme_color_override("font_color", FAINT)
	column.add_child(footer)


func _add_scroller(scroller_name: String, holder: VBoxContainer) -> ScrollContainer:
	var scroller := ScrollContainer.new()
	scroller.name = scroller_name
	scroller.custom_minimum_size = Vector2(0.0, 130.0)
	scroller.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroller.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroller.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	holder.name = scroller_name + "Holder"
	holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	holder.add_theme_constant_override("separation", 4)
	scroller.add_child(holder)
	return scroller


# A row's label takes a fixed slice of the panel and wraps inside it: a row wider than the
# panel would push the panel past the viewport it was placed in.
func _wrapped(text: String, font_size: int) -> Label:
	var label := _make_label(text, font_size)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size = Vector2(150.0, 0.0)
	label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return label


func _row() -> HBoxContainer:
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 6)
	row.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return row


func _row_button(parent: HBoxContainer, button_name: String, text: String, action: Callable,
		editable := true) -> void:
	var button := Button.new()
	button.name = button_name
	button.text = text
	button.custom_minimum_size = Vector2(28, 0)
	_style_control(button)
	button.disabled = not editable
	button.pressed.connect(action)
	parent.add_child(button)


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


func _make_time_label(tracker: Array[Label]) -> Label:
	var label := _make_label("", 13, 2)
	label.custom_minimum_size = Vector2(52, 0)
	label.add_theme_color_override("font_color", Color("f0d3a0"))
	tracker.append(label)
	return label


func _note(text: String) -> Label:
	var label := _make_label(text, 13)
	label.add_theme_color_override("font_color", FAINT)
	label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	label.custom_minimum_size = Vector2(400.0, 0.0)
	return label


func _section_heading(text: String) -> Label:
	var label := _make_label(text, 13)
	label.add_theme_color_override("font_color", MUTED_GOLD)
	return label


func _make_label(text: String, font_size: int = 15, alignment: int = 0) -> Label:
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
