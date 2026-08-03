#include <cassert>
#include <iostream>
#include <memory>
#include "../../src/ui/dialog.h"
#include "../../src/ui/dialog_factories.h"
#include "test_watchdog.h"

#include <filesystem>
#include <cstdlib>

int main()
{
	test_watchdog::setup_watchdog(30);

	// Isolate HOME environment variable to temporary directory to prevent modifying user's actual config
	std::string temp_home = (std::filesystem::temp_directory_path() / "test_options_dialogs_home").string();
	std::filesystem::create_directories(temp_home + "/.cache/turbostar");
	setenv("HOME", temp_home.c_str(), 1);

	std::cout << "Testing restructured 3 Options tabbed dialogs in isolated HOME (" << temp_home << ")..." << std::endl;

	// 1. Test Editor & Workspace Settings Dialog
	auto editor_dlg = create_editor_settings_dialog();
	assert(editor_dlg != nullptr);
	assert(editor_dlg->get_title() == "Editor & Workspace Settings");
	editor_dlg->flow();
	apply_editor_settings_from_dialog(*editor_dlg);

	// 2. Test AI & Agent Settings Dialog (with dedicated Security tab)
	auto ai_dlg = create_ai_settings_dialog();
	assert(ai_dlg != nullptr);
	assert(ai_dlg->get_title() == "AI & Agent Settings");
	ai_dlg->flow();
	assert(ai_dlg->get_value("paranoid_mode").has_value());
	assert(ai_dlg->get_value("shell_display_access").has_value());
	apply_ai_settings_from_dialog(*ai_dlg);

	// 3. Test A2A & Remote Settings Dialog
	auto a2a_dlg = create_a2a_settings_dialog();
	assert(a2a_dlg != nullptr);
	assert(a2a_dlg->get_title() == "A2A & Remote Settings");
	a2a_dlg->flow();
	apply_a2a_settings_from_dialog(*a2a_dlg);

	std::cout << "All 3 Options tabbed dialogs created and verified successfully!" << std::endl;
	return 0;
}
