// Tested source file: src/ui/dialog_settings.cpp
#include <cassert>
#include <iostream>
#include <memory>
#include "config_manager.h"
#include "project_template_manager.h"
#include "test_watchdog.h"
#include "ui/dialog.h"
#include "ui/dialog_factories.h"
#include "ui/components/ui_paged_container.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_tabbed_container.h"
#include "ui/components/ui_textbox.h"

static ui_element *find_element_by_name(ui_element *elem, const std::string &target_name)
{
	if (!elem)
		return nullptr;
	if (elem->name() == target_name)
		return elem;
	auto *container = dynamic_cast<ui_container *>(elem);
	if (container) {
		for (const auto &child : container->children()) {
			auto *found = find_element_by_name(child.get(), target_name);
			if (found)
				return found;
		}
	}
	auto *tabbed = dynamic_cast<ui_tabbed_container *>(elem);
	if (tabbed) {
		for (size_t i = 0; ; ++i) {
			auto *page = tabbed->get_tab_page(i);
			if (!page)
				break;
			auto *found = find_element_by_name(page, target_name);
			if (found)
				return found;
		}
	}
	auto *paged = dynamic_cast<ui_paged_container *>(elem);
	if (paged) {
		for (size_t i = 0; i < paged->page_count(); ++i) {
			auto *page = paged->get_page(i);
			if (page) {
				auto *found = find_element_by_name(page, target_name);
				if (found)
					return found;
			}
		}
	}
	return nullptr;
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_watchdog::scoped_test_home home_guard("dialog_lang_settings");

	auto &cfg = config_manager::get_instance();
	cfg.set_primary_language("C++");
	cfg.set_primary_language_version("C++23");

	std::cout << "Testing Language tab in Editor & Workspace Settings Dialog..." << std::endl;

	// Case 1: Initial C++ / C++23 loading
	auto editor_dlg = create_editor_settings_dialog();
	assert(editor_dlg != nullptr);
	editor_dlg->flow();

	auto lang_val = editor_dlg->get_value("primary_language");
	assert(lang_val.has_value());
	assert(*lang_val == "C++");

	auto ver_val = editor_dlg->get_value("primary_language_version");
	assert(ver_val.has_value());
	assert(*ver_val == "C++23");

	apply_editor_settings_from_dialog(*editor_dlg);
	assert(cfg.get_primary_language() == "C++");
	assert(cfg.get_primary_language_version() == "C++23");

	// Case 2: Dynamic language switch to Python
	auto *lang_elem = find_element_by_name(editor_dlg.get(), "primary_language");
	assert(lang_elem != nullptr);
	auto *lang_radio = dynamic_cast<ui_radiobutton_group *>(lang_elem);
	assert(lang_radio != nullptr);

	bool switched = lang_radio->select_choice("Python");
	assert(switched);
	assert(editor_dlg->get_value("primary_language") == "Python");
	assert(editor_dlg->get_value("primary_language_version") == "3.11+");

	apply_editor_settings_from_dialog(*editor_dlg);
	assert(cfg.get_primary_language() == "Python");
	assert(cfg.get_primary_language_version() == "3.11+");

	// Case 3: Dynamic language switch to Rust
	switched = lang_radio->select_choice("Rust");
	assert(switched);
	assert(editor_dlg->get_value("primary_language") == "Rust");
	assert(editor_dlg->get_value("primary_language_version") == "2021 Edition");

	auto *ver_elem = find_element_by_name(editor_dlg.get(), "primary_language_version");
	assert(ver_elem != nullptr);
	auto *ver_radio = dynamic_cast<ui_radiobutton_group *>(ver_elem);
	assert(ver_radio != nullptr);

	// Test SystemVerilog
	switched = lang_radio->select_choice("SystemVerilog");
	assert(switched);
	assert(editor_dlg->get_value("primary_language") == "SystemVerilog");
	assert(editor_dlg->get_value("primary_language_version") == "IEEE 1800-2017");
	for (const auto &child : ver_radio->children()) {
		assert(child->width() >= child->natural_width());
	}

	// Case 4: Select "Other" and set a custom standard string
	switched = lang_radio->select_choice("Rust");
	assert(switched);
	switched = ver_radio->select_choice("Other");
	assert(switched);
	assert(editor_dlg->get_value("primary_language_version") == "Other");

	auto *custom_elem = find_element_by_name(editor_dlg.get(), "custom_standard");
	assert(custom_elem != nullptr);
	auto *custom_tb = dynamic_cast<ui_textbox *>(custom_elem);
	assert(custom_tb != nullptr);
	custom_tb->set_buffer("Rust 2024");

	apply_editor_settings_from_dialog(*editor_dlg);
	assert(cfg.get_primary_language() == "Rust");
	assert(cfg.get_primary_language_version() == "Rust 2024");

	// Case 5: Re-opening dialog when config already has a custom standard
	cfg.set_primary_language("C++");
	cfg.set_primary_language_version("C++26");

	auto dlg2 = create_editor_settings_dialog();
	assert(dlg2 != nullptr);
	dlg2->flow();

	assert(dlg2->get_value("primary_language") == "C++");
	assert(dlg2->get_value("primary_language_version") == "Other");
	assert(dlg2->get_value("custom_standard") == "C++26");

	apply_editor_settings_from_dialog(*dlg2);
	assert(cfg.get_primary_language() == "C++");
	assert(cfg.get_primary_language_version() == "C++26");

	// Case 6: Verify shared language presets consistency with New Project Wizard
	auto wizard_dlg = create_new_project_dialog();
	assert(wizard_dlg != nullptr);
	wizard_dlg->flow();

	const auto &presets = turbostar::project_template_manager::get_language_presets();
	assert(!presets.empty());
	for (const auto &preset : presets) {
		const auto *found = turbostar::project_template_manager::find_language_preset(preset.language);
		assert(found != nullptr);
		assert(found->language == preset.language);
		assert(!found->standards.empty());
		assert(!found->buildsystems.empty());
		assert(!found->default_standard.empty());
		assert(!found->default_buildsystem.empty());
	}

	// Case 7: Verify Wizard Page 2 SystemVerilog full standard width
	auto *wizard_paged = dynamic_cast<ui_paged_container *>(find_element_by_name(wizard_dlg.get(), "wizard"));
	assert(wizard_paged != nullptr);

	auto *wiz_lang_elem = find_element_by_name(wizard_dlg.get(), "language");
	assert(wiz_lang_elem != nullptr);
	auto *wiz_lang_radio = dynamic_cast<ui_radiobutton_group *>(wiz_lang_elem);
	assert(wiz_lang_radio != nullptr);
	switched = wiz_lang_radio->select_choice("SystemVerilog");
	assert(switched);

	wizard_paged->set_current_page(2);
	auto *wiz_std_elem = find_element_by_name(wizard_dlg.get(), "language_standard");
	assert(wiz_std_elem != nullptr);
	auto *wiz_std_radio = dynamic_cast<ui_radiobutton_group *>(wiz_std_elem);
	assert(wiz_std_radio != nullptr);

	for (const auto &child : wiz_std_radio->children()) {
		assert(child->width() >= child->natural_width());
		if (child->name() == "IEEE 1800-2017") {
			assert(child->width() >= 18);
		}
	}

	std::cout << "All language tab and shared preset tests passed successfully!" << std::endl;
	return 0;
}
