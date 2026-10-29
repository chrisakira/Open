#include "open/move_order.hpp"

#include <cmath>

namespace open {
namespace {

bool usable(float value) {
	return std::isfinite(value);
}

}  // namespace

bool MoveOrder::set_destination(float x, float y, float z) {
	if (!usable(x) || !usable(y) || !usable(z)) {
		return false;
	}
	destination_ = Vec3{x, y, z};
	has_destination_ = true;
	return true;
}

void MoveOrder::clear() {
	destination_ = Vec3{};
	has_destination_ = false;
}

std::optional<Vec3> MoveOrder::destination() const {
	if (!has_destination_) {
		return std::nullopt;
	}
	return destination_;
}

bool MoveOrder::has_destination() const {
	return has_destination_;
}

Vec3 MoveOrder::velocity_at(float px, float py, float pz, float speed) const {
	(void)py;
	if (!has_destination_) {
		return {};
	}
	if (!usable(px) || !usable(pz) || !usable(speed) || speed < 0.0f) {
		return {};
	}
	if (!usable(destination_.x) || !usable(destination_.z)) {
		return {};
	}
	const float dx = destination_.x - px;
	const float dz = destination_.z - pz;
	const float length = std::sqrt(dx * dx + dz * dz);
	if (!usable(length) || length <= MoveOrder::kArriveRadius) {
		return {};
	}
	const float scale = speed / length;
	return Vec3{dx * scale, 0.0f, dz * scale};
}

}  // namespace open
