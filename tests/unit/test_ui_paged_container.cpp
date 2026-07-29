#include <cassert>
#include <iostream>
#include <memory>
#include "../../src/ui/components/ui_checkbox_group.h"
#include "../../src/ui/components/ui_paged_container.h"
#include "../../src/ui/components/ui_textbox.h"
#include "../../src/ui/dialog.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Testing ui_paged_container wizard widget..." << std::endl;

	// 1. Construct Paged Container & Dialog
	auto dlg = std::make_unique<dialog>("Wizard Dialog", 40, 15);
	auto wizard_ptr = std::make_unique<ui_paged_container>("wizard");
	auto *wizard = wizard_ptr.get();
	dlg->add_child(std::move(wizard_ptr));

	// 2. Add 3 Pages
	auto page1 = std::make_unique<ui_textbox>("p1_input", 20, "default");
	auto page2 = std::make_unique<ui_checkbox_group>("p2_options", 0, 0, 25, 4);
	auto page3 = std::make_unique<ui_textbox>("p3_confirm", 20, "confirm");

	wizard->add_page(std::move(page1));
	wizard->add_page(std::move(page2));
	wizard->add_page(std::move(page3));

	assert(wizard->page_count() == 3);
	assert(wizard->current_page() == 0);

	// 3. Verify Page 0 Focusables (Back button hidden on first page)
	auto focus_p0 = wizard->get_focusable_elements();
	bool found_back = false;
	for (auto *elem : focus_p0) {
		if (elem->name() == "wizard_back") {
			found_back = true;
		}
	}
	assert(!found_back);

	// 4. Test Navigation & Page Entry Callback
	size_t entered_page_idx = 999;
	wizard->set_page_entered_callback([&](size_t page) {
		entered_page_idx = page;
	});

	bool advanced = wizard->next_page();
	assert(advanced == true);
	assert(wizard->current_page() == 1);
	assert(entered_page_idx == 1);

	// Page 1 should show Back button
	auto focus_p1 = wizard->get_focusable_elements();
	found_back = false;
	for (auto *elem : focus_p1) {
		if (elem->name() == "wizard_back") {
			found_back = true;
		}
	}
	assert(found_back);

	// 5. Test Page Validation Interception
	bool allow_advance = false;
	wizard->set_validate_page_callback([&](size_t page, std::string &/*out_err*/) {
		if (page == 1 && !allow_advance) {
			return false;
		}
		return true;
	});

	// Next page attempt should be blocked by validation
	bool blocked = wizard->next_page();
	assert(blocked == false);
	assert(wizard->current_page() == 1);

	// Allow advance and retry
	allow_advance = true;
	bool advanced_again = wizard->next_page();
	assert(advanced_again == true);
	assert(wizard->current_page() == 2);
	assert(entered_page_idx == 2);

	// 6. Test Back Navigation
	bool went_back = wizard->previous_page();
	assert(went_back == true);
	// 7. Verify Dialog Drawing & Layout Flow Non-Infinite Loop
	dlg->flow();
	dlg->draw();

	std::cout << "All ui_paged_container wizard tests passed!" << std::endl;
	return 0;
}
