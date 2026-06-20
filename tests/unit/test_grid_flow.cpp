#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "ui/components/ui_grid_flow.h"

// A simple mock ui_element with fixed natural dimensions for layout testing.
class mock_element : public ui_element
{
      public:
	mock_element(std::string name, int w, int h)
	    : ui_element(std::move(name), 0, 0, w, h), nat_w_(w), nat_h_(h)
	{
	}

	int natural_width() const override { return nat_w_; }
	int natural_height() const override { return nat_h_; }
	bool is_focusable() const override { return true; }

	void draw(int /*abs_x*/, int /*abs_y*/) const override {}
	bool handle_event(const editor_event & /*ev*/, int /*abs_x*/, int /*abs_y*/) override { return false; }

      private:
	int nat_w_;
	int nat_h_;
};

void test_uniform_sizing()
{
	std::cout << "Testing uniform cell sizing..." << std::endl;
	ui_grid_flow grid("grid", 2, 2, 2, 1, 0); // columns = 0 (auto-wrap)
	grid.add_child(std::make_unique<mock_element>("c1", 10, 5));
	grid.add_child(std::make_unique<mock_element>("c2", 6, 8));

	// Perform layout flow with unconstrained/large container width
	grid.set_width(50);
	grid.set_height(30);
	grid.flow();

	const auto children = grid.get_focusable_elements();
	assert(children.size() == 2);

	// Both elements must receive the uniform cell size: max(10, 6) = 10 width, max(5, 8) = 8 height
	assert(children[0]->width() == 10);
	assert(children[0]->height() == 8);
	assert(children[1]->width() == 10);
	assert(children[1]->height() == 8);
}

void test_dynamic_wrapping()
{
	std::cout << "Testing dynamic column wrapping..." << std::endl;
	// Padding: x_offset = 2, y_offset = 2
	// Spacing: h_spacer = 2, v_spacer = 1
	ui_grid_flow grid("grid", 2, 2, 2, 1, 0);
	for (int i = 0; i < 5; ++i) {
		grid.add_child(std::make_unique<mock_element>("c" + std::to_string(i), 10, 8));
	}

	// Allocated container width is 50.
	// avail_w = 50 - 4 = 46.
	// cols = (46 + 2) / (10 + 2) = 48 / 12 = 4 columns.
	grid.set_width(50);
	grid.set_height(30);
	grid.flow();

	const auto children = grid.get_focusable_elements();
	assert(children.size() == 5);

	// Row 0, Col 0
	assert(children[0]->x() == 2);
	assert(children[0]->y() == 2);

	// Row 0, Col 1
	assert(children[1]->x() == 14); // 2 + 10 + 2
	assert(children[1]->y() == 2);

	// Row 0, Col 2
	assert(children[2]->x() == 26); // 14 + 10 + 2
	assert(children[2]->y() == 2);

	// Row 0, Col 3
	assert(children[3]->x() == 38); // 26 + 10 + 2
	assert(children[3]->y() == 2);

	// Row 1, Col 0 (should wrap to next row)
	assert(children[4]->x() == 2);
	assert(children[4]->y() == 11); // 2 + 8 + 1 (v_spacer)
}

void test_fixed_columns()
{
	std::cout << "Testing fixed columns mode..." << std::endl;
	// columns = 3
	ui_grid_flow grid("grid", 2, 2, 2, 1, 3);
	for (int i = 0; i < 5; ++i) {
		grid.add_child(std::make_unique<mock_element>("c" + std::to_string(i), 10, 8));
	}

	grid.set_width(100); // large width, but fixed columns should still wrap at 3
	grid.set_height(30);
	grid.flow();

	const auto children = grid.get_focusable_elements();
	assert(children.size() == 5);

	// Row 0, Col 0, 1, 2
	assert(children[0]->x() == 2);
	assert(children[1]->x() == 14);
	assert(children[2]->x() == 26);

	// Row 1, Col 0 (wraps at 3 columns)
	assert(children[3]->x() == 2);
	assert(children[3]->y() == 11);

	// Row 1, Col 1
	assert(children[4]->x() == 14);
	assert(children[4]->y() == 11);
}

void test_natural_dimensions()
{
	std::cout << "Testing natural dimensions calculations..." << std::endl;
	ui_grid_flow grid("grid", 2, 2, 2, 1, 0); // columns = 0
	for (int i = 0; i < 4; ++i) {
		grid.add_child(std::make_unique<mock_element>("c" + std::to_string(i), 10, 8));
	}

	// 4 children, unconstrained: ceil(sqrt(4)) = 2 columns, 2 rows.
	// natural_width = 2 * x_offset (4) + 2 * cell_width (20) + 1 * h_spacer (2) = 26
	// natural_height = 2 * y_offset (4) + 2 * cell_height (16) + 1 * v_spacer (1) = 21
	assert(grid.natural_width() == 26);
	assert(grid.natural_height() == 21);

	// Now constrain width to 15 (can only fit 1 column: cols = 1, rows = 4)
	grid.set_width(15);
	// natural_height should wrap to 1 column:
	// rows = 4
	// natural_height = 2 * y_offset (4) + 4 * cell_height (32) + 3 * v_spacer (3) = 39
	assert(grid.natural_height() == 39);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_uniform_sizing();
	test_dynamic_wrapping();
	test_fixed_columns();
	test_natural_dimensions();
	std::cout << "ui_grid_flow layout tests completed successfully!" << std::endl;
	return 0;
}
