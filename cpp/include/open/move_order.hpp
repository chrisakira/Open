#pragma once

#include <optional>

namespace open {

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

// A destination the character is walking to. Movement is horizontal; Y is
// recorded but never entered. Invalid input is refused so a bad click cannot
// destroy a working order.
class MoveOrder {
public:
	static constexpr float kArriveRadius = 0.4f;

	bool set_destination(float x, float y, float z);
	void clear();
	std::optional<Vec3> destination() const;
	bool has_destination() const;

	// Low level: the caller supplies the speed, so this order is reusable by any
	// mover. Character supplies its own computed speed.
	Vec3 velocity_at(float px, float py, float pz, float speed) const;

private:
	Vec3 destination_{};
	bool has_destination_ = false;
};

}  // namespace open
