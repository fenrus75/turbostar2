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

	// Advance to page 2 (with group boxes and radio groups) and verify draw does not hang
	// 8. Test Radio Group Tab Navigation
	auto r_dlg = std::make_unique<dialog>("Radio Test", 40, 15);
	auto t1 = std::make_unique<ui_textbox>("t1", 10, "first");
	auto rbg = std::make_unique<ui_radiobutton_group>("rbg", false);
	auto r1 = std::make_unique<ui_radio_choice>("r1", "Option 1", '1', true);
	auto r2 = std::make_unique<ui_radio_choice>("r2", "Option 2", '2', false);
	auto *r1_ptr = r1.get();
	auto t2_ptr = std::make_unique<ui_textbox>("t2", 10, "second");
	auto *t2_raw = t2_ptr.get();

	rbg->add_child(std::move(r1));
	rbg->add_child(std::move(r2));

	r_dlg->add_child(std::move(t1));
	r_dlg->add_child(std::move(rbg));
	r_dlg->add_child(std::move(t2_ptr));

	r_dlg->focus_first();
	editor_event tab_ev;
	tab_ev.type = event_type::key_press;
	tab_ev.key_code = '\t';

	// Tab from t1 into rbg (focuses r1)
	r_dlg->handle_event(tab_ev, 0, 0);
	assert(r1_ptr->has_focus());

	// Tab from r1 (should exit rbg and focus t2)
	r_dlg->handle_event(tab_ev, 0, 0);
	assert(t2_raw->has_focus());

	// 9. Test Checkbox Group Tab Navigation
	auto c_dlg = std::make_unique<dialog>("Checkbox Test", 40, 15);
	auto ct1 = std::make_unique<ui_textbox>("ct1", 10, "first");
	auto cbg = std::make_unique<ui_checkbox_group>("cbg", 0, 0, 25, 4);
	auto cb1 = std::make_unique<ui_checkbox>("cb1", "Option A", 'a', true);
	auto cb2 = std::make_unique<ui_checkbox>("cb2", "Option B", 'b', false);
	auto *cb1_ptr = cb1.get();
	auto ct2_ptr = std::make_unique<ui_textbox>("ct2", 10, "second");
	auto *ct2_raw = ct2_ptr.get();

	cbg->add_child(std::move(cb1));
	cbg->add_child(std::move(cb2));

	c_dlg->add_child(std::move(ct1));
	c_dlg->add_child(std::move(cbg));
	c_dlg->add_child(std::move(ct2_ptr));

	c_dlg->focus_first();

	// Tab from ct1 into cbg (focuses cb1)
	c_dlg->handle_event(tab_ev, 0, 0);
	assert(cb1_ptr->has_focus());

	// Tab from cb1 (should exit cbg and focus ct2)
	c_dlg->handle_event(tab_ev, 0, 0);
	assert(ct2_raw->has_focus());

	std::cout << "All ui_paged_container wizard tests passed!" << std::endl;
	return 0;
}
