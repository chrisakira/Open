#include "open/attributes.hpp"

#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace open {
namespace {

std::size_t slot(Attribute attribute) {
	return static_cast<std::size_t>(static_cast<int>(attribute));
}

bool fits_float(double value) {
	return std::isfinite(value) && value <= static_cast<double>(std::numeric_limits<float>::max());
}

double effective_at(const std::array<float, static_cast<std::size_t>(Attribute::Count)> &bases,
					Attribute attribute, int level, Attribute blessing) {
	const double multiplier = static_cast<double>(level_multiplier(level));
	const double blessing_factor =
		attribute == blessing ? static_cast<double>(kBlessingMultiplier) : 1.0;
	return static_cast<double>(bases[slot(attribute)]) * multiplier * blessing_factor;
}

}  // namespace

Attributes::Attributes(int level, Attribute blessing) {
	if (!valid(level)) {
		throw std::invalid_argument("Attributes: level must be 1..7");
	}
	if (!valid(blessing)) {
		throw std::invalid_argument("Attributes: unknown blessing id");
	}
	bases_.fill(kBaseStart);
	level_ = level;
	blessing_ = blessing;
}

int Attributes::level() const {
	return level_;
}

Attribute Attributes::blessing() const {
	return blessing_;
}

float Attributes::base(Attribute attribute) const {
	if (!valid(attribute)) {
		throw std::invalid_argument("Attributes::base: unknown attribute id");
	}
	return bases_[slot(attribute)];
}

float Attributes::effective(Attribute attribute) const {
	if (!valid(attribute)) {
		throw std::invalid_argument("Attributes::effective: unknown attribute id");
	}
	return static_cast<float>(effective_at(bases_, attribute, level_, blessing_));
}

bool Attributes::overflows_at(int level) const {
	for (std::size_t i = 0; i < bases_.size(); ++i) {
		if (!fits_float(effective_at(bases_, static_cast<Attribute>(i), level, blessing_))) {
			return true;
		}
	}
	return false;
}

bool Attributes::set_level(int level) {
	if (!valid(level)) {
		return false;
	}
	if (overflows_at(level)) {
		return false;
	}
	level_ = level;
	return true;
}

bool Attributes::set_base(Attribute attribute, float base) {
	if (!valid(attribute)) {
		return false;
	}
	if (!std::isfinite(base) || base <= 0.0f) {
		return false;
	}
	const double blessing_factor =
		attribute == blessing_ ? static_cast<double>(kBlessingMultiplier) : 1.0;
	const double candidate =
		static_cast<double>(base) * static_cast<double>(level_multiplier(level_)) * blessing_factor;
	if (!fits_float(candidate)) {
		return false;
	}
	bases_[slot(attribute)] = base;
	return true;
}

Attribute random_blessing(std::mt19937_64 &rng) {
	std::uniform_int_distribution<int> draw(0, static_cast<int>(Attribute::Count) - 1);
	return static_cast<Attribute>(draw(rng));
}

}  // namespace open
