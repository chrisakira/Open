#include "open/move_order.hpp"

#include <cmath>
#include <iostream>

static int g_failed = 0;

static void expect_vec(const char *name, const open::Vec3 &got, float x, float y, float z) {
	if (std::fabs(got.x - x) > 0.0001f || std::fabs(got.y - y) > 0.0001f ||
		std::fabs(got.z - z) > 0.0001f) {
		std::cerr << "FAIL " << name << ": got (" << got.x << ", " << got.y << ", " << got.z << ")\n";
		g_failed++;
	}
}

int main() {
	{
		open::MoveOrder order;
		order.set_destination(10.0f, 0.0f, 0.0f);
		expect_vec("walks toward destination at given speed", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f),
				   4.5f, 0.0f, 0.0f);
	}
	{
		// Break this catches: a mover that keeps walking when already inside the arrive radius.
		open::MoveOrder order;
		order.set_destination(0.3f, 0.0f, 0.0f);
		expect_vec("stops inside arrive radius", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f,
				   0.0f);
	}
	{
		// Break this catches: a mover that flies in Y toward the click point.
		open::MoveOrder order;
		order.set_destination(10.0f, 8.0f, 0.0f);
		expect_vec("ignores height and walks on the ground",
				   order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 4.5f, 0.0f, 0.0f);
	}
	if (g_failed != 0) {
		return 1;
	}
	std::cout << "ok\n";
	return 0;
}
