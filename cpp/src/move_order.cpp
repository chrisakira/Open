#include "open/move_order.hpp"

#include <cmath>

namespace open {

void MoveOrder::set_destination(float x, float y, float z) {
	destination_ = Vec3{x, y, z};
	has_destination_ = true;
}

Vec3 MoveOrder::velocity_at(float px, float py, float pz, float speed) const {
	if (!has_destination_) {
		return {};
	}
	(void)py;
	const float dx = destination_.x - px;
	const float dz = destination_.z - pz;
	const float length = std::sqrt(dx * dx + dz * dz);
	if (length <= MoveOrder::kArriveRadius) {
		return {};
	}
	const float scale = speed / length;
	return Vec3{dx * scale, 0.0f, dz * scale};
}

}  // namespace open

