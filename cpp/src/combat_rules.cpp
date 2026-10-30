#include "open/combat_rules.hpp"

#include <cmath>
#include <random>

namespace open {
namespace {

// The parts a melee hit can take: the whole BodyPart enum except Count.
constexpr int kMeleePartFaces = 6;

bool usable(float value) {
	return std::isfinite(value);
}

}  // namespace

bool melee_in_range(const Vec3 &self, const Vec3 &target) {
	if (!usable(self.x) || !usable(self.z) || !usable(target.x) || !usable(target.z)) {
		return false;
	}
	const float dx = target.x - self.x;
	const float dz = target.z - self.z;
	// A huge separation overflows the square to infinity, which is out of range
	// rather than in it.
	const float length = std::sqrt(dx * dx + dz * dz);
	return usable(length) && length <= kMeleeRange;
}

BodyPart random_melee_part(std::mt19937_64 &rng) {
	// One roll, six faces, in enum order: Head, Torso, LeftArm, RightArm,
	// LeftLeg, RightLeg. Count is a sentinel, never a face.
	std::uniform_int_distribution<int> die(0, kMeleePartFaces - 1);
	return static_cast<BodyPart>(die(rng));
}

}  // namespace open
