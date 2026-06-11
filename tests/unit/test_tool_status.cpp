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

	// Helper function to recursively find all labels and OK button
	auto find_elements_recursive = [&](auto &self, const ui_container &container) -> void {
		const auto &sub_children = test_container_accessor::get_children(container);
		for (const auto &child : sub_children) {
			std::cout << "Inspecting element: name=" << child->name() << std::endl;
			if (child->name() == "btn_ok") {
				found_ok_btn = true;
			} else if (child->name() == "text") {
				label_count++;
			}
			auto *sub_container = dynamic_cast<const ui_container *>(child.get());
			if (sub_container) {
				self(self, *sub_container);
			}
		}
	};

	find_elements_recursive(find_elements_recursive, *dlg);

	assert(found_ok_btn);
	// We expect at least one header label, plus status labels, plus either install message or success message
	assert(label_count >= 6);

	// Test create_model_edit_dialog with copilot format
	{
		std::cout << "Testing create_model_edit_dialog with copilot..." << std::endl;
		auto model = std::make_shared<agentlib::ai_model>("copilot-model", "Copilot model", "https://api.githubcopilot.com", "purpose", 0.0, 0.0, "", agentlib::api_type::copilot);
		auto edit_dlg = create_model_edit_dialog(model);
		assert(edit_dlg != nullptr);

		auto api_type_val = edit_dlg->get_value("api_type");
		assert(api_type_val.has_value());
		assert(*api_type_val == "copilot");
		std::cout << "Model edit dialog copilot test passed!" << std::endl;
	}

	// Test create_ask_user_dialog
	{
		std::cout << "Testing create_ask_user_dialog..." << std::endl;
		auto ask_dlg = create_ask_user_dialog("Confirm status?", {"Yes", "No"});
		assert(ask_dlg != nullptr);
		assert(ask_dlg->width() >= 70);
		std::cout << "Ask user dialog test passed!" << std::endl;
	}

	std::cout << "Tool status dialog unit test passed successfully!" << std::endl;
	return 0;
}
