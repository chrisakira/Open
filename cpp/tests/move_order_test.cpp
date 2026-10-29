#include "open/move_order.hpp"

#include <cmath>
#include <limits>

#include "test_support.hpp"

// Move order tests. The low-level speed argument stays reusable; Character
// supplies its own computed speed. Break these catches: an order that survives
// a clear, a garbage destination replacing a good one, a mover that flies in Y,
// or a mover that walks on a NaN.

int main() {
	{
		open::MoveOrder order;
		expect_true("an order is accepted", order.set_destination(10.0f, 0.0f, 0.0f));
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
		// Break this catches: the boundary itself being off by a hair.
		open::MoveOrder order;
		order.set_destination(0.4f, 0.0f, 0.0f);
		expect_vec("exactly the arrive radius stops", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f,
				   0.0f);
		order.set_destination(0.41f, 0.0f, 0.0f);
		expect_true("just outside the arrive radius moves",
					order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f).x > 0.0f);
	}
	{
		// Break this catches: a mover that flies in Y toward the click point.
		open::MoveOrder order;
		order.set_destination(10.0f, 8.0f, 0.0f);
		expect_vec("ignores height and walks on the ground",
				   order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 4.5f, 0.0f, 0.0f);
	}
	{
		// Break this catches: no order being confused with an order to stay still.
		open::MoveOrder order;
		expect_true("an empty order has no destination", !order.destination().has_value());
		expect_vec("an empty order does not move", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f,
				   0.0f);
		order.set_destination(5.0f, 0.0f, 2.0f);
		expect_true("the destination is queryable", order.destination().has_value());
		order.clear();
		expect_true("clear removes it", !order.destination().has_value());
		expect_vec("clear stops the mover", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f, 0.0f);
	}
	{
		// Break this catches: a bad destination destroying a working order.
		const float nan = std::numeric_limits<float>::quiet_NaN();
		open::MoveOrder order;
		order.set_destination(10.0f, 0.0f, 0.0f);
		expect_true("a nan destination is refused", !order.set_destination(nan, 0.0f, 0.0f));
		expect_true("the old destination survives", order.destination()->x == 10.0f);
		expect_true("an infinite destination is refused",
					!order.set_destination(std::numeric_limits<float>::infinity(), 0.0f, 0.0f));
		expect_true("the old destination still survives", order.destination()->x == 10.0f);
		expect_true("a nan axis is refused too", !order.set_destination(1.0f, nan, 3.0f));
		expect_close("the mover kept walking", order.velocity_at(0.0f, 0.0f, 0.0f, 4.5f).x, 4.5f);
	}
	{
		// Break this catches: garbage position or speed producing NaN velocity that
		// would fly through the world.
		const float nan = std::numeric_limits<float>::quiet_NaN();
		open::MoveOrder order;
		order.set_destination(10.0f, 0.0f, 0.0f);
		expect_vec("a nan position does not move", order.velocity_at(nan, 0.0f, 0.0f, 4.5f), 0.0f, 0.0f,
				   0.0f);
		expect_vec("an infinite position does not move",
				   order.velocity_at(0.0f, 0.0f, std::numeric_limits<float>::infinity(), 4.5f), 0.0f, 0.0f,
				   0.0f);
		expect_vec("a negative speed does not walk backwards", order.velocity_at(0.0f, 0.0f, 0.0f, -4.5f),
				   0.0f, 0.0f, 0.0f);
		expect_vec("a nan speed does not move", order.velocity_at(0.0f, 0.0f, 0.0f, nan), 0.0f, 0.0f, 0.0f);
		expect_vec("an infinite speed does not move",
				   order.velocity_at(0.0f, 0.0f, 0.0f, std::numeric_limits<float>::infinity()), 0.0f, 0.0f,
				   0.0f);
		expect_vec("zero speed is allowed and still", order.velocity_at(0.0f, 0.0f, 0.0f, 0.0f), 0.0f, 0.0f,
				   0.0f);
	}

	return test_summary();
}
