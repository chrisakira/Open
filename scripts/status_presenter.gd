# The status presenter: the one read path from a sheet into the status views.
#
# It owns exactly two thoughts and no node, no clock, and no state. `read_sheet` takes a fresh,
# disposable mirror of what one `OpenCharacter` currently says — the bridge getters, once each,
# on demand — and the pool formatters decide how a value prints (whole numbers, ties half-up)
# and how a bar fills (current over *its own* maximum, never a fixed
# 100). A snapshot is never authority: nothing here can write into a sheet, and every view must
# re-read instead of caching, so a changed maximum shows in the very next frame.
#
# Domain: configured sheets with finite values, positive maxima and current within maximum —
# the views' gate (G6) keeps anything else away. Like `combat_text.gd`, deliberately *not*
# reusing the damage-label rule: that one refuses to print a positive as zero, and this one
# prints exactly what it is told, because the bar's fraction keeps tiny positives alive.

static func read_sheet(sheet: OpenCharacter) -> Dictionary:
	return {
		"character_id": int(sheet.get_character_id()),
		"character_name": String(sheet.get_character_name()),
		"level": int(sheet.get_level()),
		"faction": int(sheet.get_faction()),
		"alive": bool(sheet.is_alive()),
		"hp": _pool(float(sheet.get_health()), float(sheet.get_max_health())),
		"stamina": _pool(float(sheet.get_stamina()), float(sheet.get_max_stamina())),
		"mana": _pool(float(sheet.get_mana()), float(sheet.get_max_mana())),
	}


# One pool as the views want it: current and maximum as C++ just answered them.
static func _pool(current: float, maximum: float) -> Dictionary:
	return {"current": current, "maximum": maximum}


# How full a bar is: the pool's own current maximum is the whole bar. No rounding — the fill
# is geometry, not text.
static func pool_fraction(current: float, maximum: float) -> float:
	return current / maximum


# The player readout, e.g. `HP 100/100`: both numbers through the same half-up formatter
# so a tie can never round differently on one side of the slash than the other.
static func pool_text(label: String, current: float, maximum: float) -> String:
	return "%s %s" % [label, pool_amounts(current, maximum)]


# The same readout without the leading label, for rows whose name lives in its own column.
static func pool_amounts(current: float, maximum: float) -> String:
	return "%s/%s" % [_amount(current), _amount(maximum)]


# The locked name policy (human G3, 2026-11-01), one rule for both views: the sheet's own name,
# falling back to the caller's scene-node name when the sheet has none, truncated to 19
# characters plus an ellipsis past twenty. Explicit test coverage comes with the world view.
static func display_name(sheet_name: String, fallback_name: String) -> String:
	var chosen := sheet_name if sheet_name != "" else fallback_name
	if chosen.length() > 20:
		return chosen.substr(0, 19) + "…"
	return chosen


# Whole numbers, half-up: 12.4 → 12, 12.5 → 13. This affects text only, not the fill.
static func _amount(value: float) -> String:
	return "%d" % int(floorf(value + 0.5))
