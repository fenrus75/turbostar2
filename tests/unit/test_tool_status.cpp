#include <cassert>
#include <iostream>
#include "../../src/ui/dialog_factories.h"
#include "../../src/ui/dialog.h"
#include "../../src/ui/ui_element.h"

// Accessor subclass to inspect protected children_ of dialog/ui_container
class test_dialog_accessor : public dialog
{
public:
	static const std::vector<std::unique_ptr<ui_element>> &get_children(const dialog &d)
	{
		return static_cast<const test_dialog_accessor &>(d).children_;
	}
};

int main()
{
	std::cout << "Testing create_tool_status_dialog..." << std::endl;

	auto dlg = create_tool_status_dialog();
	assert(dlg != nullptr);

	const auto &children = test_dialog_accessor::get_children(*dlg);
	assert(!children.empty());

	bool found_ok_btn = false;
	int label_count = 0;

	for (const auto &child : children) {
		std::cout << "Child element: name=" << child->name() << std::endl;
		if (child->name() == "btn_ok") {
			found_ok_btn = true;
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
