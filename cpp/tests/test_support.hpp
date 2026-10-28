#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <new>

// Shared helpers for the headless C++ tests. Existing style is expect_true /
// expect_vec; close() is finite-aware on purpose: fabs(NaN) > tolerance does
// not detect NaN, so every float comparison here requires both sides finite.

inline int g_failed = 0;

inline void expect_true(const char *name, bool cond) {
	if (!cond) {
		std::cerr << "FAIL " << name << "\n";
		g_failed++;
	}
}

inline bool close(float got, float want, float tolerance = 0.0001f) {
	return std::isfinite(got) && std::isfinite(want) && std::fabs(got - want) < tolerance;
}

inline void expect_close(const char *name, float got, float want, float tolerance = 0.0001f) {
	if (!close(got, want, tolerance)) {
		std::cerr << "FAIL " << name << ": got " << got << ", want " << want << "\n";
		g_failed++;
	}
}

inline void expect_throws_invalid(const char *name, const std::function<void ()> &fn) {
	bool thrown = false;
	bool wrong_type = false;
	try {
		fn();
	} catch (const std::invalid_argument &) {
		thrown = true;
	} catch (...) {
		wrong_type = true;
	}
	if (wrong_type) {
		std::cerr << "FAIL " << name << ": threw something other than std::invalid_argument\n";
		g_failed++;
	} else if (!thrown) {
		std::cerr << "FAIL " << name << ": no std::invalid_argument\n";
		g_failed++;
	}
}

inline int test_summary() {
	if (g_failed != 0) {
		std::cerr << g_failed << " failure(s)\n";
		return 1;
	}
	std::cout << "ok\n";
	return 0;
}
