#pragma once

namespace open {

struct Vec3 {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

class MoveOrder {
public:
	static constexpr float kArriveRadius = 0.4f;

	void set_destination(float x, float y, float z);
	Vec3 velocity_at(float px, float py, float pz, float speed) const;

private:
	Vec3 destination_{};
	bool has_destination_ = false;
};

}  // namespace open
