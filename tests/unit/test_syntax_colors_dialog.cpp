#include <cassert>
#include <iostream>
#include <memory>
#include "ui/dialog_factories.h"
#include "ui/dialog.h"
#include "ui/components/ui_color_picker.h"
#include "ui/components/ui_listbox.h"
#include "syntax_color_manager.h"
#include "event_queue.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(10);
	std::cout << "Running test_syntax_colors_dialog..." << std::endl;

	// Initialize the syntax color manager
	syntax_color_manager::get_instance().initialize();

	// Create the dialog
	std::unique_ptr<dialog> dlg = create_syntax_colors_dialog();
	assert(dlg != nullptr);

	// Find the color picker and listbox
	ui_color_picker* picker = nullptr;
	ui_listbox* listbox = nullptr;

	for (const auto& child : dlg->children()) {
		if (child->name() == "color_picker") {
			picker = dynamic_cast<ui_color_picker*>(child.get());
		} else if (child->name() == "attribute_list") {
			listbox = dynamic_cast<ui_listbox*>(child.get());
		}
	}

	assert(picker != nullptr);
	assert(listbox != nullptr);

	// Select the first item in the listbox
	listbox->set_selected_index(0);

	// Simulate a mouse click on the color picker's background color row.
	// We want to trigger the picker's callback, which accesses the listbox.
	// Click on Background color 5 (which is valid and triggers the set_color callback).
	// Background blocks are 4 characters wide.
	// Coordinates relative to picker's start: x = 5 * 4 = 20. y = 3 (Background row).
	editor_event ev;
	ev.type = event_type::mouse_click;
	ev.mouse_x = picker->x() + 20;
	ev.mouse_y = picker->y() + 3;

	// Clobber the stack to ensure the dangling stack reference is overwritten and crashes
	struct Clobber {
		static void use_some_stack(int depth) {
			if (depth <= 0) return;
			volatile double arr[50];
			double sum = 0.0;
			for (int i = 0; i < 50; ++i) {
				arr[i] = 12.34 + i;
				sum += arr[i];
			}
			(void)sum;
			use_some_stack(depth - 1);
		}
	};
	Clobber::use_some_stack(20);

	std::cout << "Simulating click at X=" << ev.mouse_x << " Y=" << ev.mouse_y << std::endl;
	
	// This will invoke handle_event, triggering the lambda callback.
	// If the listbox pointer was a dangling reference, it will SIGSEGV/crash right here!
	bool handled = picker->handle_event(ev, picker->x(), picker->y());
	assert(handled);

	std::cout << "test_syntax_colors_dialog passed successfully!" << std::endl;
	return 0;
}
