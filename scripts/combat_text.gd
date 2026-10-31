# The one way combat presentation prints an amount, in the two shapes it is needed in.
#
# This rule belongs to the floating damage labels and is moved here rather than rewritten, so
# the action chat and the number over a body's head cannot disagree about what C++ reported. The
# figures themselves are the simulation's: `damage` is the amount that was handed to the sheet,
# and nothing here rounds it, clamps it or re-derives it.
#
# Five decimals, then whatever is left: a rank-1 blow is worth under a hundredth of damage, and
# a display that rounds it to zero would be reporting that nothing happened when C++ said
# something did. A big hit keeps its whole number for the same reason.

static func damage(amount: float) -> String:
	var text := String.num(amount, 5)
	while text.ends_with("0") and not text.ends_with(".0"):
		text = text.substr(0, text.length() - 1)
	if text.ends_with(".0"):
		text = text.substr(0, text.length() - 1)
	if amount > 0.0 and float(text) <= 0.0:
		# Five fixed decimals cannot hold this small a positive, and "0" would report
		# that nothing arrived when C++ says something did. The engine's own default
		# conversion does not round away a positive: it just gets longer about it.
		text = String.num(amount)
	return text


# The same figure for the middle of a sentence. A whole amount leaves the label form holding a
# bare point - today's output, pinned before this moved so the labels keep it exactly - and in
# prose that reads as a typo rather than a number. Only the trailing point is touched: no
# amount is ever rounded, so no positive amount can arrive at "0" by way of this function.
static func amount(value: float) -> String:
	var text := damage(value)
	if text.ends_with("."):
		text = text.substr(0, text.length() - 1)
	return text
