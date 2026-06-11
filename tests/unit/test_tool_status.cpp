#include <cassert>
#include <iostream>
#include "../../src/ui/dialog_factories.h"
#include "../../src/ui/dialog.h"
#include "../../src/ui/ui_element.h"

// Accessor subclass to inspect protected children_ of ui_container
class test_container_accessor : public ui_container
{
public:
	static const std::vector<std::unique_ptr<ui_element>> &get_children(const ui_container &c)
	{
		return static_cast<const test_container_accessor &>(c).children_;
	}
};

int main()
{
	std::cout << "Testing create_tool_status_dialog..." << std::endl;

	auto dlg = create_tool_status_dialog();
	assert(dlg != nullptr);

	const auto &children = test_container_accessor::get_children(*dlg);
	assert(!children.empty());

	bool found_ok_btn = false;
	int label_count = 0;

	for (const auto &child : children) {
		std::cout << "Child element: name=" << child->name() << std::endl;
		if (child->name() == "btn_ok") {
			found_ok_btn = true;
		} else if (child->name() == "buttons") {
			const auto &sub_children = test_container_accessor::get_children(*static_cast<const ui_container *>(child.get()));
			for (const auto &sub_child : sub_children) {
				if (sub_child->name() == "btn_ok") {
					found_ok_btn = true;
				}
			}
		} else if (child->name() == "text") {
			label_count++;
			assert(child->x() + child->width() < dlg->width() - 1);
		}
	}

	assert(found_ok_btn);
	// We expect at least one header label, plus status labels, plus either install message or success message
	assert(label_count >= 6);

	std::cout << "Tool status dialog unit test passed successfully!" << std::endl;
	return 0;
}
