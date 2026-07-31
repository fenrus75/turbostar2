#include <cassert>
#include <iostream>
#include <memory>
#include "../../src/ui/components/ui_checkbox.h"
#include "../../src/ui/components/ui_tabbed_container.h"
#include "../../src/ui/components/ui_textbox.h"
#include "../../src/ui/dialog.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Testing ui_tabbed_container widget..." << std::endl;

	// 1. Construct Dialog & Tabbed Container
	auto dlg = std::make_unique<dialog>("Tabbed Test", 60, 20);
	auto tabbed_ptr = std::make_unique<ui_tabbed_container>("tabs", 0, 0, 58, 18);
	auto *tabbed = tabbed_ptr.get();
	dlg->add_child(std::move(tabbed_ptr));

	// 2. Add Tab Pages
	auto p1 = std::make_unique<ui_textbox>("tab1_txt", 20, "val1");
	auto p2 = std::make_unique<ui_checkbox>("tab2_chk", "Option 2", '2', true);

	tabbed->add_tab_page("general", "General", std::move(p1));
	tabbed->add_tab_page("build", "Build & Format", std::move(p2));

	assert(tabbed->tab_count() == 2);
	assert(tabbed->active_tab() == 0);

	// 3. Test Tab Switching by Index & ID
	size_t changed_tab_index = 99;
	std::string changed_tab_id = "";
	tabbed->set_tab_changed_callback([&](size_t idx, const std::string &id) {
		changed_tab_index = idx;
		changed_tab_id = id;
	});

	bool switched = tabbed->set_active_tab_by_id("build");
	assert(switched == true);
	assert(tabbed->active_tab() == 1);
	assert(changed_tab_index == 1);
	assert(changed_tab_id == "build");

	tabbed->set_active_tab(0);
	assert(tabbed->active_tab() == 0);

	// 4. Test Form Value Lookup across all tabs
	auto val1 = tabbed->get_value("tab1_txt");
	assert(val1.has_value());
	assert(*val1 == "val1");

	auto val2 = tabbed->get_value("tab2_chk");
	assert(val2.has_value());
	assert(*val2 == "true");

	// 5. Test Focus Movements between Sidebar and Page Content
	dlg->focus_first();
	assert(tabbed->has_focus());

	// Use dlg->handle_key('\t') which cycles focus through get_focusable_elements()
	dlg->handle_key('\t');

	auto *active_page = tabbed->get_active_page();
	assert(active_page != nullptr);
	assert(active_page->has_focus());

	// Flow and Layout calculations
	dlg->flow();

	std::cout << "All ui_tabbed_container tests passed successfully!" << std::endl;
	return 0;
}
