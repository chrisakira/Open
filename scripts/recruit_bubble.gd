# The answer bubble: the one word a character said, hung over the head that said it.
#
# It is a verdict display and nothing more. C++ settled the answer and stamped whose ask it
# answered; the scene that issued the ask carries that answer here, in one word, for one moment.
# This control decides nothing: it never asks whether somebody *could* be asked, never re-reads a
# condition to guess at what was said, and never keeps an answer on screen past the moment it was
# given - it has no door back into a sheet, because it has no door at all.
#
# What it does hold is the human's timing, flagged 2026-11-04: about a second of a word anybody
# can finish reading, then most of a second of ink going away, and no movement besides following
# the head it points at. A card that rose, popped or animated would be a card competing with the
# fight it is reporting.
#
# It borrows the status overlay's anchor and gate rather than inventing a second rule for where a
# head is and who can be seen: the same projection, the same head height, and the same
# hide-when-the-camera-cannot-show-it. A verdict nobody can see is not a verdict worth faking, so
# an off-screen or out-of-range answer is simply not shown while it is out of range - and, like
# every other status view, it is click-through and has nothing on it to click.
extends Control

const Plate := preload("res://scripts/world_status.gd")

# The badge wood and brass of the level plate, with the same parchment ink the names wear: the
# verdict is the one word on the card, so nothing here shouts it in another colour (the human's
# call, flagged 2026-11-04 - a NO is not an error, it is an answer).
const CARD_WOOD := Color("#493326")
const CARD_BRASS := Color("#806343")
const INK := Color("#F4E6C6")

const WORD_YES := "YES"
const WORD_NO := "NO"

const HOLD := 1.0
const FADE := 0.6

const CARD_WIDTH := 46.0
const CARD_HEIGHT := 24.0
const TAIL_HEIGHT := 8.0
const TAIL_HALF_WIDTH := 7.0
const CARD_BORDER := 2.0
# The plate the verdict stacks over, at the height the plate itself says it has: the card does not
# get to keep a second opinion about where the plate ends.
const PLATE_HEIGHT := Plate.PLATE_HEIGHT
const STACK_GAP := Plate.GAP

var _overlay: Control
var _actor: Node
var _answering := false
var _elapsed := 0.0
var _word: Label


func _init() -> void:
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	custom_minimum_size = Vector2(CARD_WIDTH, CARD_HEIGHT + TAIL_HEIGHT)
	# After the world's camera follow (0) and after the overlay (50): Godot 4 ticks the *lower*
	# priority first, so a negative number here would have made this the earliest of the three and
	# the card would have hung from last frame's camera.
	process_priority = 60
	_word = Label.new()
	_word.name = "AnswerWord"
	_word.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_word.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	_word.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	var serif := SystemFont.new()
	serif.font_names = PackedStringArray(["Georgia", "DejaVu Serif", "Liberation Serif", "serif"])
	_word.add_theme_font_override("font", serif)
	_word.add_theme_font_size_override("font_size", 13)
	_word.add_theme_color_override("font_color", INK)
	_word.position = Vector2(0.0, 0.0)
	_word.size = Vector2(CARD_WIDTH, CARD_HEIGHT)
	add_child(_word)
	visible = false


# The overlay that owns the projection and the gate this card obeys. Bound once by the scene that
# builds the HUD: the bubble does not look for a camera, it asks the one view that already decides
# what the player can see.
func bind_view(overlay: Control) -> void:
	_overlay = overlay


# Show that this character answered, and say what the answer said. Calling it again with another
# actor (or the same one) is a new answer: the clock restarts, because the word being read is the
# one just said.
func open_for(actor: Node, accepted: bool) -> void:
	_actor = actor
	_word.text = WORD_YES if accepted else WORD_NO
	_answering = true
	_elapsed = 0.0
	modulate = Color(1.0, 1.0, 1.0, 1.0)
	_follow()


# The word this card is wearing right now, or "" when it has nothing to say. A test reads it; a
# player reads the word, not this door.
func word() -> String:
	return String(_word.text)


# One frame of the card's own life: the hold, then the fade, then the card is gone. Called by
# `_process` and by nothing else; the scene never pushes time into an answer. The clock runs only
# while the gate is showing the card: time spent hidden is time nobody could read, and spending
# it would take the answer away before it was ever seen.
func tick(delta: float) -> void:
	if not _answering:
		return
	_follow()
	if not visible:
		return
	_elapsed += delta
	if _elapsed >= HOLD + FADE:
		_retire()
		return
	if _elapsed > HOLD:
		var fading := minf(1.0, (_elapsed - HOLD) / FADE)
		modulate.a = 1.0 - fading
	else:
		modulate.a = 1.0


func _process(delta: float) -> void:
	tick(delta)


# Follow the head, or hide when the camera cannot show that head. Hiding is not retiring: the
# answer keeps its clock *where it stopped*, so an answer that walks out of range comes back on
# the word it was already given and at the ink it had reached - it neither loses the moment nor
# gets a second one for free.
func _follow() -> void:
	if _actor == null or not is_instance_valid(_actor):
		_retire()
		return
	if _overlay == null or not is_instance_valid(_overlay):
		visible = false
		return
	# The camera's own answer, not the plate's: the card can be pointed at a character the
	# overlay owns no plate for - the player has none - and "can this head be seen" is the
	# question either way.
	if not bool(_overlay.call("camera_shows", _actor)):
		visible = false
		return
	var anchor: Vector2 = _overlay.call("head_anchor", _actor)
	if anchor == Vector2.INF:
		visible = false
		return
	position = Vector2(anchor.x - size.x / 2.0, anchor.y - PLATE_HEIGHT - STACK_GAP - size.y)
	visible = true


func _retire() -> void:
	_answering = false
	_elapsed = 0.0
	_word.text = ""
	visible = false
	modulate = Color(1.0, 1.0, 1.0, 1.0)


func _draw() -> void:
	# The card first, a brass edge and a wood field, then the tail below it pointing at the
	# head: the same badge the level plate wears, cut to one word.
	var card := Rect2(Vector2.ZERO, Vector2(CARD_WIDTH, CARD_HEIGHT))
	draw_rect(card, CARD_BRASS, false, CARD_BORDER)
	draw_rect(card.grow(-CARD_BORDER), CARD_WOOD)
	var tip := Vector2(CARD_WIDTH / 2.0, CARD_HEIGHT + TAIL_HEIGHT)
	draw_polygon(PackedVector2Array([
		Vector2(CARD_WIDTH / 2.0 - TAIL_HALF_WIDTH, CARD_HEIGHT),
		Vector2(CARD_WIDTH / 2.0 + TAIL_HALF_WIDTH, CARD_HEIGHT),
		tip,
	]), PackedColorArray([CARD_BRASS]))
	var inner := maxf(TAIL_HALF_WIDTH - CARD_BORDER * 2.0, 1.0)
	var inner_tip := tip - Vector2(0.0, CARD_BORDER)
	draw_polygon(PackedVector2Array([
		Vector2(CARD_WIDTH / 2.0 - inner, CARD_HEIGHT),
		Vector2(CARD_WIDTH / 2.0 + inner, CARD_HEIGHT),
		inner_tip,
	]), PackedColorArray([CARD_WOOD]))


