#include "open/character.hpp"

#include <cmath>
#include <iostream>

static int g_failed = 0;

static void expect_true(const char *name, bool cond) {
	if (!cond) {
		std::cerr << "FAIL " << name << "\n";
		g_failed++;
	}
}

static void expect_vec(const char *name, const open::Vec3 &got, float x, float y, float z) {
	if (std::fabs(got.x - x) > 0.0001f || std::fabs(got.y - y) > 0.0001f ||
		std::fabs(got.z - z) > 0.0001f) {
		std::cerr << "FAIL " << name << ": got (" << got.x << ", " << got.y << ", " << got.z << ")\n";
		g_failed++;
	}
}

int main() {
	{
		// Break this catches: a character missing faction, level, stats, or a limb.
		const open::Character c = open::Character::make(open::Faction::Friendly, 1);
		expect_true("friendly faction", c.faction() == open::Faction::Friendly);
		expect_true("level 1", c.level() == 1);
		expect_true("health 100", std::fabs(c.health() - 100.0f) < 0.0001f);
		expect_true("max health 100", std::fabs(c.max_health() - 100.0f) < 0.0001f);
		expect_true("strength 10", std::fabs(c.strength() - 10.0f) < 0.0001f);
		expect_true("left arm", c.limbs().left_arm);
		expect_true("right arm", c.limbs().right_arm);
		expect_true("left leg", c.limbs().left_leg);
		expect_true("right leg", c.limbs().right_leg);
	}
	{
		// Break this catches: an immovable character that still walks to a destination.
		open::Character c = open::Character::make(open::Faction::Enemy, 1);
		c.set_can_move(false);
		c.set_destination(10.0f, 0.0f, 0.0f);
		expect_vec("immovable character stays put", c.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f,
				   0.0f);
	}
	if (g_failed != 0) {
		return 1;
	}
	std::cout << "ok\n";
	return 0;
}
