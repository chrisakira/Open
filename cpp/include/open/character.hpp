#pragma once

#include "open/move_order.hpp"

namespace open {

enum class Faction { Friendly, Enemy };

struct Limbs {
	bool left_arm = false;
	bool right_arm = false;
	bool left_leg = false;
	bool right_leg = false;
};

class Character {
public:
	static Character make(Faction faction, int level);

	Faction faction() const;
	int level() const;
	float health() const;
	float max_health() const;
	float strength() const;
	Limbs limbs() const;
	bool can_move() const;
	void set_can_move(bool can_move);
	void set_destination(float x, float y, float z);
	Vec3 velocity_at(float px, float py, float pz, float speed) const;

private:
	Faction faction_ = Faction::Enemy;
	int level_ = 0;
	float health_ = 0.0f;
	float max_health_ = 0.0f;
	float strength_ = 0.0f;
	Limbs limbs_{};
	bool can_move_ = true;
	MoveOrder order_{};
};

}  // namespace open
