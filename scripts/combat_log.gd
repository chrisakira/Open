class_name CombatLog
extends Control

# The bottom-left action chat: one truthful line per thing the simulation did.
#
# This control decides nothing. C++ settles what a swing was worth and which defence met it, the
# bridge carries that report, and `CombatFeedback` is the one node that drains the queue - it
# hands the same event to the clip, the floating number and these lines. So the chat never
# reaches for a coordinator, never drains anything a second time, and never infers a defence from
# a number: a zero may mean a full prevent, an armour soak or a blow with nothing in it, and a
# prevention of zero may mean a worthless block or no block at all. Only the report says which.
#
# It is a log of *reports*, not a live view: a line says what happened and then stays out of the
# way. Nothing here can be clicked into an order, and no line fades on a timer.

const CombatText := preload("res://scripts/combat_text.gd")

# docs/style.md: the main panel's dark warm brown, the muted-brass frame, and the warm parchment
# body text. The panel is translucent so the fight it describes stays visible behind it, and the
# text keeps enough contrast to read over the world without the frame competing with it.
const PANEL_COLOR := Color(0.141, 0.106, 0.086, 0.86)
const FRAME_COLOR := Color(0.502, 0.388, 0.263, 0.9)
const TEXT_COLOR := Color(0.878, 0.816, 0.682, 1.0)
const FRAME_WIDTH := 2.0
const SCREEN_MARGIN := 16.0
const INNER_PADDING := 10.0
const FONT_SIZE := 16
const LINE_SPACING := 3.0

# The locked history: eight entries, newest at the bottom, oldest dropped. Eight *entries* - an
# entry that wraps onto three screen lines is still the one swing it reports.
const MAX_ENTRIES := 8

# Viewport-relative limits, so the panel is modest at 1280x720 and still modest at half that:
# the width is an anchor and the height a cap, both fractions of whatever the window is.
const WIDTH_FRACTION := 0.42
const HEIGHT_FRACTION := 0.45

var _actors: Array = []
var _entries: PackedStringArray = PackedStringArray()
var _lines: VBoxContainer
var _built := false


func _init() -> void:
	# Built here rather than in `_ready` because the chat is handed events by another node, and
	# a report that arrives before the scene finished its first frame is still a report.
	_build()


func _ready() -> void:
	_build()
	var view := get_viewport()
	if view != null and not view.size_changed.is_connected(_layout):
		view.size_changed.connect(_layout)
	_layout()


func _notification(what: int) -> void:
	# Re-measured when the panel enters a viewport and whenever its own rect changes, which is
	# how it follows the window: an anchored control is resized by the engine when the viewport
	# is, and this is the notice that arrives.
	if what == NOTIFICATION_POST_ENTER_TREE or what == NOTIFICATION_RESIZED:
		_layout()


# The actors the world was built from, kept as a list to look names up in. Held loosely on
# purpose: an actor can be freed while its lines are still on screen, and a stale node is never
# trusted to answer for a character.
func bind_actors(actors: Array) -> void:
	_actors = actors


# One performed swing, one line. The event is read and never written: the same dictionary goes
# on to the clip and the damage label, and a listener that edited it would change them.
func append_swing(event: Dictionary) -> void:
	_build()
	var attacker := _name_of(int(event.get("actor_id", 0)))
	var target := _name_of(int(event.get("target_id", 0)))
	var defense := String(event.get("defense_action", "none"))
	_entries.append(_sentence(attacker, target, defense, float(event.get("damage", 0.0))))
	while _entries.size() > MAX_ENTRIES:
		_entries.remove_at(0)
	_rebuild()
	_layout()


# --- the sentences -------------------------------------------------------------

# The four locked shapes, each of them half of a pair the numbers cannot tell apart. The verb is
# the defence C++ said it ran and nothing else: an absent or unrecognised answer is no answer,
# and the amount is printed only when some damage got through. A zero-damage defence may mean the
# armour took the rest, so no wording here claims the defence prevented everything.
func _sentence(attacker: String, target: String, defense: String, damage: float) -> String:
	match defense:
		"block", "dodge":
			var verb := "blocks" if defense == "block" else "dodges"
			if damage > 0.0:
				return "%s attacks but %s %s and receives %s damage" % [
						attacker, target, verb, CombatText.amount(damage)]
			return "%s attacks but %s %s" % [attacker, target, verb]
		_:
			if damage > 0.0:
				return "%s attacks %s and deals %s damage" % [
						attacker, target, CombatText.amount(damage)]
			return "%s attacks %s" % [attacker, target]


# Scene names, never sheet names: an empty sheet name is not a thing to read aloud, and the
# world's node names are what the player is looking at. An id the world cannot answer - unknown,
# or freed since - prints its id rather than borrowing somebody else's name.
func _name_of(actor_id: int) -> String:
	for actor in _actors:
		if not is_instance_valid(actor):
			continue
		var sheet = actor.get("sheet")
		if sheet == null or not sheet.is_configured():
			continue
		if int(sheet.get_character_id()) == actor_id:
			return String(actor.name)
	return "Character %d" % actor_id


# --- the panel -----------------------------------------------------------------

func _build() -> void:
	if _built:
		return
	_built = true
	# Click-through, on the panel and on everything in it: the world is behind this thing, and a
	# floor click that landed on a line would silently become no click at all.
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	clip_contents = true
	# Bottom-left, and a fraction of the viewport wide: the anchors do the following-the-window
	# work that a hand-computed position would only get right at one size.
	anchor_left = 0.0
	anchor_top = 1.0
	anchor_right = WIDTH_FRACTION
	anchor_bottom = 1.0
	grow_horizontal = Control.GROW_DIRECTION_END
	grow_vertical = Control.GROW_DIRECTION_BEGIN
	offset_left = SCREEN_MARGIN
	offset_right = -SCREEN_MARGIN
	offset_bottom = -SCREEN_MARGIN
	offset_top = -SCREEN_MARGIN
	_lines = VBoxContainer.new()
	_lines.name = "Entries"
	_lines.mouse_filter = Control.MOUSE_FILTER_IGNORE
	# Bottom-anchored and growing upward, because a container is never smaller than the entries
	# it holds: pinned by its bottom edge, eight long entries spill above the panel - where it
	# clips them - instead of pushing the newest line below the screen.
	_lines.anchor_left = 0.0
	_lines.anchor_top = 1.0
	_lines.anchor_right = 1.0
	_lines.anchor_bottom = 1.0
	_lines.grow_vertical = Control.GROW_DIRECTION_BEGIN
	_lines.offset_left = INNER_PADDING
	_lines.offset_right = -INNER_PADDING
	_lines.offset_top = -INNER_PADDING
	_lines.offset_bottom = -INNER_PADDING
	# Newest at the bottom, and the oldest end clipped when eight entries wrap past the cap: the
	# swing that just happened is the one worth reading.
	_lines.alignment = BoxContainer.ALIGNMENT_END
	_lines.add_theme_constant_override("separation", int(LINE_SPACING))
	add_child(_lines)
	# The entries know their own wrapped height before this node does, so when their box changes
	# size the panel is measured again.
	_lines.size_changed.connect(_layout)


func _rebuild() -> void:
	_build()
	for child in _lines.get_children():
		child.free()
	for text in _entries:
		_lines.add_child(_line(String(text)))


# Plain text, no markup: the names it prints come from the scene, and a character called
# "[b]Bold[/b]" should read as exactly that.
func _line(text: String) -> Label:
	var line := Label.new()
	line.text = text
	line.mouse_filter = Control.MOUSE_FILTER_IGNORE
	line.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	# No `clip_text` here: with autowrap it makes the label report a one-pixel minimum height,
	# which collapses the whole panel. Overflow is the panel's business - it clips its own
	# contents - and a word too long for a line is clipped by that instead.
	line.add_theme_font_size_override("font_size", FONT_SIZE)
	line.add_theme_color_override("font_color", TEXT_COLOR)
	line.size_flags_horizontal = Control.SIZE_FILL
	line.size_flags_stretch_ratio = 1.0
	return line


# Where the panel sits and how tall it gets. The bottom-left corner and the width are anchors,
# not arithmetic: a fraction of the viewport, held to the corner it is meant to sit in, so the
# panel follows the window wherever it goes rather than only at the size it happened to be built
# at. Only the height is measured, because that is the one dimension the entries decide - and it
# is capped, so eight long entries wrap and clip inside the panel instead of walking off the top.
func _layout() -> void:
	var view := get_viewport()
	if view == null:
		return
	var limits: Vector2 = view.get_visible_rect().size
	if limits.x <= 0.0 or limits.y <= 0.0:
		return
	var cap := maxf(limits.y * HEIGHT_FRACTION - 2.0 * SCREEN_MARGIN, _single_line_height())
	# Once the entries know their width they measure themselves, and the box they are in reports
	# that back; until then the same font says how tall they will be.
	var entries := _lines.get_combined_minimum_size().y
	if entries <= 0.0:
		var inner_width := maxf(limits.x * WIDTH_FRACTION - 2.0 * (SCREEN_MARGIN + INNER_PADDING),
				16.0)
		entries = _measured_entries_height(inner_width)
	var height := clampf(entries + 2.0 * INNER_PADDING,
			_single_line_height() + 2.0 * INNER_PADDING, cap)
	offset_top = -SCREEN_MARGIN - height
	queue_redraw()


# The height the entries need at this width, wrapped: what the panel would be if the viewport
# never said otherwise.
func _measured_entries_height(inner_width: float) -> float:
	var total := 0.0
	for entry in _entries:
		total += _entry_height(String(entry), inner_width) + LINE_SPACING
	return total


# How tall one entry is at this width: the wrapped line count, from the same font the label
# draws with. Measured rather than guessed, because the chat is the only place a long name has
# to fit, and a panel that grew one line per entry would be a panel that walks off the top.
func _entry_height(text: String, inner_width: float) -> float:
	var font: Font = get_theme_font("font")
	if font == null:
		return _single_line_height()
	# The wrapped height the same font reports at this width, read back as a line count: the
	# measurement and the drawing cannot disagree, because they are the same font.
	var line_height: float = maxf(font.get_height(FONT_SIZE), 1.0)
	var measured: Vector2 = font.get_multiline_string_size(text, HORIZONTAL_ALIGNMENT_LEFT,
			int(inner_width), FONT_SIZE, -1)
	if measured.y <= 0.0:
		return line_height
	var lines := maxi(int(round(measured.y / line_height)), 1)
	return float(lines) * line_height + float(lines - 1) * LINE_SPACING


func _single_line_height() -> float:
	var font: Font = get_theme_font("font")
	if font == null:
		return float(FONT_SIZE) + LINE_SPACING
	return maxf(font.get_height(FONT_SIZE), 1.0)


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	draw_rect(rect, FRAME_COLOR)
	draw_rect(rect.grow(-FRAME_WIDTH), PANEL_COLOR)
