# The squad plaque: who belongs to the squad, printed on the right edge and always on.
#
# This control decides nothing. Membership is `OpenCharacter::is_in_squad` and C++ writes it -
# by the coordinator's founding and by the answer to an ask - so this is a mirror with no opinions
# of its own: it never hides because a squad happens to be empty, because an empty squad is still
# the squad, and the heading is what says so. It also never infers a happening from a snapshot:
# that is the chat's and the answer bubble's business, and a row appearing is not a news story,
# it is just who is seated right now.
#
# The order the rows come back in is the one the human approved: the founder at the top, then the
# rest by character id. The founder is not inferred - this is presentation, and C++ keeps no
# "who founded" query - so the scene that asked the coordinator to seat somebody says who that
# was. Names follow the locked policy through the presenter (the sheet's name, the scene node's
# name underneath it, cut past twenty), so a character is one name across the whole HUD.
#
# It is a view and only a view: nothing here writes a flag, a faction, a membership or an order,
# and it has no control a click could turn into an order - it ignores the mouse, panel and every
# label on it, so a click beside the names is a click on the world.
extends Control

const Presenter := preload("res://scripts/status_presenter.gd")

# docs/style.md, the same plaque family as the player's: dark wood inside a brass frame inside a
# dark cast band, the parchment name and the brass heading.
const WOOD := Color("#241B16")
const BRASS := Color("#806343")
const CAST_BAND := Color("#2E231C")
const HEADING_COLOR := Color("#BD9862")
const NAME_COLOR := Color("#F4E6C6")

# The right screen edge, 16 px in and vertically centred (docs/recruiting.md, "The squad
# plaque"). The height is what the rows need; the width is the locked plaque width.
const SIDE_MARGIN := 16.0
const PLAQUE_WIDTH := 176.0
const PADDING := 8.0
const HEADING_HEIGHT := 16.0
const ROW_HEIGHT := 18.0
const ROW_GAP := 2.0
const HEADING_TEXT := "SQUAD"

var _actors: Array = []
var _founder: Node
var _rows: Dictionary = {} # actor instance id -> Label
var _heading: Label
var _built := false


func _init() -> void:
	# Built here rather than in `_ready`, because a bind that arrives before the first frame is
	# still a bind, and a plaque that only exists after the scene finished building would show
	# nothing on the frame the squad was founded.
	_build()


func _ready() -> void:
	_build()


# Every drawn frame re-reads who is seated. No caller has to remember to refresh: the row that a
# join or a death adds or takes away is on screen the next frame, and a display that needed an
# external tick would be a display that can be stale on purpose.
func _process(_delta: float) -> void:
	refresh()


func _notification(what: int) -> void:
	if what == Control.NOTIFICATION_RESIZED:
		_layout()


# Replace the whole bound squad: the actors to read membership from, and the one the scene asked
# the coordinator to seat. Held loosely, the way the chat holds them: an actor can be freed while
# its row is still on the plaque, and a stale node is never trusted to answer for a character.
func bind_squad(actors: Array, founder: Node) -> void:
	_build()
	_actors = actors
	_founder = founder
	refresh()


# The per-frame seam: read the sheets now, show that now.
func refresh() -> void:
	var seated := _seated()
	_reconcile(seated)
	_reorder(seated)
	_set_height(seated.size())
	_layout()


# --- who is seated ---------------------------------------------------------------

# The sheets that say they are in the squad, in the locked order: the founder if the founder is
# one of them, then everybody else by character id. Faction, flag and condition are read by
# nobody here: being hostile, askable or hopeful is not being seated.
func _seated() -> Array:
	var members: Array = []
	for actor in _actors:
		if actor == null or not is_instance_valid(actor):
			continue
		var sheet: OpenCharacter = actor.get("sheet")
		if sheet == null or not is_instance_valid(sheet) or not sheet.is_configured():
			continue
		if not bool(sheet.is_in_squad()):
			continue
		members.append({
			"actor": actor,
			"sheet": sheet,
			"id": int(sheet.get_character_id()),
			"founder": actor == _founder,
		})
	members.sort_custom(_seat_then_id)
	return members


func _seat_then_id(left: Dictionary, right: Dictionary) -> bool:
	if bool(left["founder"]) != bool(right["founder"]):
		return bool(left["founder"])
	return int(left["id"]) < int(right["id"])


# One Label per seated actor, keyed by the actor's *instance*, never by numeric id alone: a freed
# actor's row is released, and a fresh node wearing an old id reads fresh identity.
func _reconcile(seated: Array) -> void:
	var keep: Dictionary = {}
	for member in seated:
		var actor: Node = member["actor"]
		var actor_id := actor.get_instance_id()
		keep[actor_id] = true
		if not _rows.has(actor_id):
			var label := _row(member)
			add_child(label)
			_rows[actor_id] = label
		else:
			(_rows[actor_id] as Label).text = _name_for(member)
	for actor_id in _rows.keys():
		if not keep.has(actor_id):
			var retired: Label = _rows[actor_id]
			_rows.erase(actor_id)
			# Deleted, not queued: a row whose actor left the squad belongs to nobody else,
			# and a retirement that survives into the next frame would print a name that is
			# already not in the squad.
			retired.free()


func _row(member: Dictionary) -> Label:
	var label := Label.new()
	# Named for the character, not the seat: an id keeps its full 64-bit width here, and a row
	# that a test or a future scene looks up by name finds exactly one.
	label.name = "Member_%d" % int(member["id"])
	label.text = _name_for(member)
	# Clipped to the row: the name rule cuts at twenty characters, not at a pixel count, so a name
	# of wide letters still asks for more room than the plaque has. A Label refuses to be narrower
	# than its text unless it clips, which is what pushed the row past the brass.
	label.clip_text = true
	label.mouse_filter = Control.MOUSE_FILTER_IGNORE
	label.add_theme_font_override("font", _serif())
	label.add_theme_font_size_override("font_size", 13)
	label.add_theme_color_override("font_color", NAME_COLOR)
	return label


func _name_for(member: Dictionary) -> String:
	var sheet: OpenCharacter = member["sheet"]
	return Presenter.display_name(String(sheet.get_character_name()), String(member["actor"].name))


func _reorder(seated: Array) -> void:
	# The heading owns index 0 and the rows follow it in order: `move_child` re-orders without
	# anyone having to rebuild the plaque, so a name that stays seated keeps its own Label.
	var index := 1
	for member in seated:
		var actor: Node = member["actor"]
		var label: Label = _rows.get(actor.get_instance_id())
		if label != null and label.get_parent() == self:
			move_child(label, index)
			index += 1


# --- the panel -------------------------------------------------------------------

func _build() -> void:
	if _built:
		return
	_built = true
	# Click-through, on the panel and on every label in it: the world is behind this thing.
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	anchor_left = 1.0
	anchor_right = 1.0
	anchor_top = 0.5
	anchor_bottom = 0.5
	grow_horizontal = Control.GROW_DIRECTION_BEGIN
	grow_vertical = Control.GROW_DIRECTION_BOTH
	offset_left = -(SIDE_MARGIN + PLAQUE_WIDTH)
	offset_right = -SIDE_MARGIN
	_set_height(0)

	_heading = Label.new()
	_heading.name = "Heading"
	_heading.text = HEADING_TEXT
	_heading.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_heading.add_theme_font_override("font", _serif())
	_heading.add_theme_font_size_override("font_size", 11)
	_heading.add_theme_color_override("font_color", HEADING_COLOR)
	add_child(_heading)


# The height is the rows: an empty squad is the heading and its margins, and every name after
# that adds one row of its own.
func _set_height(row_count: int) -> void:
	var stack := 0.0
	if row_count > 0:
		stack = ROW_GAP + row_count * ROW_HEIGHT + (row_count - 1) * ROW_GAP
	var height := 2.0 * PADDING + HEADING_HEIGHT + stack
	offset_top = -height / 2.0
	offset_bottom = height / 2.0


func _layout() -> void:
	var inner_width := maxf(size.x - 2.0 * PADDING, 0.0)
	_heading.position = Vector2(PADDING, PADDING)
	_heading.size = Vector2(inner_width, HEADING_HEIGHT)
	var y := PADDING + HEADING_HEIGHT + ROW_GAP
	for index in range(1, get_child_count()):
		var row := get_child(index) as Label
		if row == null:
			continue
		row.position = Vector2(PADDING, y)
		row.size = Vector2(inner_width, ROW_HEIGHT)
		y += ROW_HEIGHT + ROW_GAP


func _serif() -> SystemFont:
	var serif := SystemFont.new()
	serif.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	return serif


# The locked plaque surface, drawn the way the player's plaque draws itself: cast band, brass
# frame, wood field.
func _draw() -> void:
	var full := Rect2(Vector2.ZERO, size)
	draw_rect(full, CAST_BAND)
	var framed := full.grow(-3.0)
	draw_rect(framed, BRASS)
	draw_rect(framed.grow(-2.0), WOOD)
