#include "open/character.hpp"

namespace open {

Character Character::make(Faction faction, int level) {
	Character c;
	c.faction_ = faction;
	c.level_ = level;
	c.max_health_ = 100.0f;
	c.health_ = 100.0f;
	c.strength_ = 10.0f;
	c.limbs_ = Limbs{true, true, true, true};
	c.can_move_ = true;
	return c;
}

Faction Character::faction() const {
	return faction_;
}

int Character::level() const {
	return level_;
}

float Character::health() const {
	return health_;
}

float Character::max_health() const {
	return max_health_;
}

float Character::strength() const {
	return strength_;
}

Limbs Character::limbs() const {
	return limbs_;
}

bool Character::can_move() const {
	return can_move_;
}

void Character::set_can_move(bool can_move) {
	can_move_ = can_move;
}

void Character::set_destination(float x, float y, float z) {
	order_.set_destination(x, y, z);
}

Vec3 Character::velocity_at(float px, float py, float pz, float speed) const {
	if (!can_move_) {
		return {};
	}
	return order_.velocity_at(px, py, pz, speed);
}

}  // namespace open
