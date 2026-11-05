class_name WorkbenchPopup
extends PanelContainer

# The bench panel: what this worker's purse can pay for, what the coordinator's fifteen
# seconds are doing right now, and one button per request. It is a view over
# `OpenCombat.get_work_snapshot` and a sender of WorkRequest dictionaries and nothing else.
#
# The rules it must not grow: affordability comes from the snapshot, the quality cap comes
# from the snapshot, the recipe list and its costs come from the snapshot, and the progress
# bar reads the coordinator's elapsed clock every frame - the panel owns no timer, because a
# second clock is a second truth and the world already has exactly one. A press emits a
# request and shows the answer, including the answer "no".

const OPEN_SIZE := Vector2(540.0, 480.0)
const OPEN_OFFSET := Vector2(10.0, 6.0)
const BENCH_SMITHING := 0
const BENCH_ENCHANTING := 1
const KIND_CRAFT := 0
const KIND_REPAIR := 1
const KIND_ENCHANT := 2
const BENCH_NAMES := {
	BENCH_SMITHING: "Smithing Workbench",
	BENCH_ENCHANTING: "Enchanting Workbench",
}
const MATERIAL_NAMES := {100: "Scrap", 101: "Rune Dust"}

const INK := Color("e0d0ae")
const FAINT := Color("9c886d")
const MUTED_GOLD := Color("bd9862")
const PANEL_COLOR := Color("241b16")
const HEADER_COLOR := Color("493326")
const BORDER_COLOR := Color("806343")
const ROW_COLOR := Color("382b21")

signal work_requested(request: Dictionary)
signal cancel_requested()

var bench_id := 0
var bench_type := BENCH_SMITHING

var _combat: OpenCombat
var _sheet  # the OpenCharacter whose schedule and purse this panel draws
var _serif_font: SystemFont
var _title_label: Label
var _status_label: Label
var _rows_holder: VBoxContainer
var _signature := ""
var _position_initialized := false
var _opening_position := Vector2.ZERO


func _ready() -> void:
	custom_minimum_size = OPEN_SIZE
	size = OPEN_SIZE
	mouse_filter = Control.MOUSE_FILTER_STOP
	modulate.a = 0.9
	visible = false
	_serif_font = SystemFont.new()
	_serif_font.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	var panel := StyleBoxFlat.new()
	panel.bg_color = PANEL_COLOR
	panel.border_color = BORDER_COLOR
	panel.set_border_width_all(2)
	panel.set_corner_radius_all(3)
	panel.set_content_margin_all(4)
	add_theme_stylebox_override("panel", panel)
	set_process(false)
	_build_skeleton()
	visibility_changed.connect(_on_visibility_changed)


func bind_worker(combat: OpenCombat, sheet) -> void:
	_combat = combat
	_sheet = sheet
	_signature = ""
	_render()


func open_for_bench(id: int, type: int, screen_position: Vector2) -> void:
	bench_id = id
	bench_type = type
	_position_initialized = false
	_opening_position = screen_position
	_signature = ""
	visible = true
	_render()


func close() -> void:
	visible = false


func covers(point: Vector2) -> bool:
	return visible and get_global_rect().has_point(point)


func _on_visibility_changed() -> void:
	set_process(visible)
	if visible:
		_render()


func _unhandled_key_input(event: InputEvent) -> void:
	if visible and event is InputEventKey and event.pressed and not event.echo \
			and (event.keycode == KEY_ESCAPE or event.physical_keycode == KEY_ESCAPE):
		close()
		get_viewport().set_input_as_handled()


func _process(_delta: float) -> void:
	if not _position_initialized:
		_place()
		_position_initialized = true
	_render()


func _place() -> void:
	var viewport_size := get_viewport_rect().size
	var wanted := _opening_position + OPEN_OFFSET
	var limit := viewport_size - size
	position = Vector2(clampf(wanted.x, 0.0, maxf(0.0, limit.x)),
		clampf(wanted.y, 0.0, maxf(0.0, limit.y)))


# --- drawing ---------------------------------------------------------------------

func _snapshot() -> Dictionary:
	if _combat == null or _sheet == null or not _sheet.is_configured():
		return {}
	return _combat.get_work_snapshot(_sheet.get_character_id())


func _render() -> void:
	var snapshot: Dictionary = _snapshot()
	if snapshot.is_empty() or not bool(snapshot.get("valid")):
		if _signature != "none":
			_signature = "none"
			_rebuild({})
		return
	var materials: Dictionary = snapshot.get("materials", {})
	var lines: Array = []
	for key in materials.keys():
		lines.append("%s=%d" % [str(key), int(materials[key])])
	lines.append("phase=%s" % str(snapshot.get("phase")))
	lines.append("failure=%s" % str(snapshot.get("failure")))
	lines.append("elapsed=%.1f" % float(snapshot.get("elapsed")))
	lines.append("cap=%d" % int(snapshot.get("quality_cap")))
	# The repair and enchant lists are read from the equipment snapshot at build time,
	# so the snapshot belongs in the watched truth: a sword that takes a scratch while
	# the panel is open has to appear in its own picker without being asked.
	for row in _sheet.get_equipment_snapshot():
		lines.append("eq=%d:%d/%d:%d/%d" % [int(row["item_id"]), int(row["durability"]),
				int(row["max_durability"]), (row["enchantments"] as Array).size(),
				int(row["enchantment_capacity"])])
	var signature := ",".join(lines)
	if signature == _signature:
		return  # same truth, same controls: nothing under the pointer gets replaced
	_signature = signature
	_rebuild(snapshot)


func _rebuild(snapshot: Dictionary) -> void:
	for child in _rows_holder.get_children():
		child.free()
	_title_label.text = String(BENCH_NAMES.get(bench_type, "Workbench"))
	if snapshot.is_empty():
		_status_label.text = "No worker to read."
		return
	_status_label.text = _status_line(snapshot)
	var busy := bool(snapshot.get("busy"))
	if busy:
		var cancel := _button("CancelButton", "Cancel")
		cancel.pressed.connect(_on_cancel_pressed)
		_rows_holder.add_child(cancel)
		return
	for recipe in snapshot.get("recipes", []):
		if int(recipe["bench"]) != bench_type:
			continue
		_build_row(recipe, snapshot)


func _status_line(snapshot: Dictionary) -> String:
	match String(snapshot.get("phase")):
		"queued":
			return "Queued - walking to the bench. Nothing is paid until you arrive."
		"active":
			return "At the bench, working: %.1f of %.1f s." % [float(snapshot.get("elapsed")),
				float(snapshot.get("duration"))]
		"completed":
			return "Done. The last job delivered."
		"failed":
			return "Last job ended: %s." % String(snapshot.get("failure"))
	return "The bench is free."


func _build_row(recipe: Dictionary, snapshot: Dictionary) -> void:
	var kind := int(recipe["kind"])
	var recipe_id := int(recipe["id"])
	var affordable := bool(recipe.get("affordable"))
	var container := VBoxContainer.new()
	container.add_theme_constant_override("separation", 2)
	_rows_holder.add_child(container)

	var materials: Dictionary = snapshot.get("materials", {})
	var bill := _bill_text(recipe)
	match kind:
		KIND_CRAFT:
			var cap := int(snapshot.get("quality_cap"))
			var output := int(recipe.get("output", 0))
			container.add_child(_label("Forge %s  ·  %s  ·  have %s" % [
				_definition_name(output), bill, _purse_text(materials)], 15, INK))
			var controls := HBoxContainer.new()
			container.add_child(controls)
			# The selector never offers a tier C++ did not unlock, and defaults to the best.
			var tiers := OptionButton.new()
			tiers.name = "Quality%d" % recipe_id
			tiers.custom_minimum_size = Vector2(88, 0)
			for tier in range(1, maxi(cap, 1) + 1):
				tiers.add_item("Tier %d" % tier, tier)
			if cap >= 1:
				tiers.select(maxi(cap, 1) - 1)
			controls.add_child(tiers)
			var forge := _button("Forge%d" % recipe_id, "Forge")
			forge.disabled = not affordable or cap < 1
			forge.pressed.connect(_on_forge_pressed.bind(recipe_id, tiers))
			controls.add_child(forge)
		KIND_REPAIR:
			var candidates := _repair_candidates()
			container.add_child(_label("Mend a dented weapon or shield  ·  %s" % bill, 15, INK))
			var picker := OptionButton.new()
			picker.name = "Target%d" % recipe_id
			picker.custom_minimum_size = Vector2(280, 0)
			for entry in candidates:
				picker.add_item(String(entry["label"]), int(entry["id"]))
			picker.disabled = candidates.is_empty()
			var mend := _button("Mend%d" % recipe_id, "Mend")
			mend.disabled = not affordable or candidates.is_empty()
			mend.pressed.connect(_on_target_action_pressed.bind(recipe_id, KIND_REPAIR, picker))
			var row := HBoxContainer.new()
			row.add_child(picker)
			row.add_child(mend)
			container.add_child(row)
		KIND_ENCHANT:
			var offers: Array = recipe.get("enchantments", [])
			var targets := _enchant_targets()
			container.add_child(_label("Fix Blunt Edge to a weapon  ·  %s" % bill, 15, INK))
			var picker := OptionButton.new()
			picker.name = "Target%d" % recipe_id
			picker.custom_minimum_size = Vector2(280, 0)
			for entry in targets:
				picker.add_item(String(entry["label"]), int(entry["id"]))
			picker.disabled = targets.is_empty() or offers.is_empty()
			var fix := _button("Enchant%d" % recipe_id, "Enchant")
			fix.disabled = not affordable or targets.is_empty() or offers.is_empty()
			fix.pressed.connect(_on_enchant_pressed.bind(recipe_id, int(offers[0]) if not offers.is_empty() else 0, picker))
			var row := HBoxContainer.new()
			row.add_child(picker)
			row.add_child(fix)
			container.add_child(row)


func _bill_text(recipe: Dictionary) -> String:
	var parts := []
	for line in recipe.get("cost", []):
		parts.append("%d %s" % [int(line["quantity"]),
			String(MATERIAL_NAMES.get(int(line["material"]), "material %d" % int(line["material"])))])
	return " + ".join(parts) if not parts.is_empty() else "free"


func _purse_text(materials: Dictionary) -> String:
	var parts := []
	for key in materials.keys():
		parts.append("%s %d" % [String(MATERIAL_NAMES.get(int(key), "material %d" % int(key))),
			int(materials[key])])
	return ", ".join(parts) if not parts.is_empty() else "an empty purse"


func _definition_name(definition_id: int) -> String:
	# The model key of the recipe's output, read from any owned instance of it: names come
	# from what the worker owns, and an unowned definition keeps its id as its name.
	if _sheet != null:
		for row in _sheet.get_equipment_snapshot():
			if int(row["definition_id"]) == definition_id:
				return String(row["model_key"])
	return "item %d" % definition_id


func _repair_candidates() -> Array:
	var entries := []
	if _sheet == null:
		return entries
	for row in _sheet.get_equipment_snapshot():
		if int(row["max_durability"]) > 0 and int(row["durability"]) < int(row["max_durability"]):
			if not _sheet.get_weapon_snapshot(int(row["item_id"])).is_empty() \
					or String(row["model_key"]) == "Shield_Round":
				entries.append({"id": int(row["item_id"]), "label": "%s %d/%d" % [
					String(row["model_key"]), int(row["durability"]), int(row["max_durability"])]})
	return entries


func _enchant_targets() -> Array:
	var entries := []
	if _sheet == null:
		return entries
	for row in _sheet.get_equipment_snapshot():
		if _sheet.get_weapon_snapshot(int(row["item_id"])).is_empty():
			continue  # not a weapon
		if (row["enchantments"] as Array).size() < int(row["enchantment_capacity"]):
			entries.append({"id": int(row["item_id"]), "label": "%s (%d/%d slots)" % [
				String(row["model_key"]), (row["enchantments"] as Array).size(),
				int(row["enchantment_capacity"])]})
	return entries


# --- presses ---------------------------------------------------------------------

func _request(kind: int, recipe_id: int, target: int, quality: int, enchantment: int) -> void:
	# Exactly the WorkRequest keys, and not one of them a claim about cost, skill or combat:
	# those are the coordinator's answers, so a crafted dictionary cannot bargain with them.
	work_requested.emit({"kind": kind, "recipe": recipe_id, "bench": bench_id,
			"target": target, "quality": quality, "enchantment": enchantment})
	# The rows were built by the press: rebuild only next frame, never inside the signal.
	_signature = ""


func _on_forge_pressed(recipe_id: int, tiers: OptionButton) -> void:
	_request(KIND_CRAFT, recipe_id, 0, int(tiers.get_item_id(tiers.selected)), 0)


func _on_target_action_pressed(recipe_id: int, kind: int, picker: OptionButton) -> void:
	_request(kind, recipe_id, int(picker.get_item_id(picker.selected)), 0, 0)


func _on_enchant_pressed(recipe_id: int, enchantment: int, picker: OptionButton) -> void:
	_request(KIND_ENCHANT, recipe_id, int(picker.get_item_id(picker.selected)), 0, enchantment)


func _on_cancel_pressed() -> void:
	cancel_requested.emit()
	_signature = ""


# --- skeleton and controls --------------------------------------------------------

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
	header.mouse_filter = Control.MOUSE_FILTER_STOP
	header.add_theme_constant_override("separation", 10)
	header_panel.add_child(header)
	_title_label = _label("Workbench", 20, Color("f4e6c6"))
	_title_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	header.add_child(_title_label)
	var close_button := Button.new()
	close_button.name = "CloseButton"
	close_button.text = "×"
	close_button.custom_minimum_size = Vector2(28, 26)
	_style_control(close_button)
	close_button.pressed.connect(close)
	header.add_child(close_button)

	_status_label = _label("", 13, FAINT)
	_status_label.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	_status_label.custom_minimum_size = Vector2(500.0, 0.0)
	column.add_child(_status_label)

	var scroller := ScrollContainer.new()
	scroller.custom_minimum_size = Vector2(0.0, 300.0)
	scroller.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroller.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	_rows_holder = VBoxContainer.new()
	_rows_holder.name = "ActionHolder"
	_rows_holder.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_rows_holder.add_theme_constant_override("separation", 10)
	scroller.add_child(_rows_holder)
	column.add_child(scroller)

	column.add_child(_label("Requests only. The bench decides everything else.", 11, FAINT))


func _button(button_name: String, text: String) -> Button:
	var button := Button.new()
	button.name = button_name
	button.text = text
	button.custom_minimum_size = Vector2(96, 0)
	_style_control(button)
	return button


func _style_control(button: Button) -> void:
	button.focus_mode = Control.FOCUS_NONE
	button.add_theme_font_override("font", _serif_font)
	button.add_theme_font_size_override("font_size", 13)
	button.add_theme_color_override("font_color", INK)
	button.add_theme_color_override("font_hover_color", Color("fff0ce"))
	button.add_theme_color_override("font_disabled_color", FAINT)
	var normal := StyleBoxFlat.new()
	normal.bg_color = ROW_COLOR
	normal.border_color = BORDER_COLOR
	normal.set_border_width_all(1)
	normal.set_content_margin_all(4)
	button.add_theme_stylebox_override("normal", normal)
	var hover := StyleBoxFlat.new()
	hover.bg_color = Color("62472f")
	hover.border_color = BORDER_COLOR
	hover.set_border_width_all(1)
	hover.set_content_margin_all(4)
	button.add_theme_stylebox_override("hover", hover)


func _label(text: String, font_size: int, color: Color = INK) -> Label:
	var label := Label.new()
	label.text = text
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_override("font", _serif_font)
	label.add_theme_color_override("font_color", color)
	label.add_theme_font_size_override("font_size", font_size)
	return label
