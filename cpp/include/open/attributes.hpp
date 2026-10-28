#pragma once

#include "open/character_rules.hpp"

#include <array>
#include <random>

namespace open {

// The ten attribute bases, the level, and the one blessing. Effective values
// are calculated on demand: base * level multiplier * blessing. Nothing here
// stores a derived value, and pools never use these multipliers.
class Attributes {
public:
	// Throws std::invalid_argument for a level outside 1..7 or a bad blessing id.
	Attributes(int level, Attribute blessing);

	int level() const;
	Attribute blessing() const;

	// Checked getters: throw std::invalid_argument for a bad attribute id.
	float base(Attribute attribute) const;
	float effective(Attribute attribute) const;

	// Mutations report failure and leave the character unchanged.
	bool set_level(int level);
	bool set_base(Attribute attribute, float base);

	static constexpr float kBaseStart = 1.0f;

private:
	bool overflows_at(int level) const;

	std::array<float, static_cast<std::size_t>(Attribute::Count)> bases_{};
	int level_ = 1;
	Attribute blessing_ = Attribute::Strength;
};

// NPC blessings come from a generator the caller owns: no global RNG, no
// clock reseeding per character.
Attribute random_blessing(std::mt19937_64 &rng);

}  // namespace open
