#include <cassert>
#include <iostream>
#include "a2a/a2a_server_manager.h"
#include "project_manager.h"
#include "test_watchdog.h"
#include "ui/dialog_factories.h"

using namespace a2a;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	a2a_server_config cfg;
	cfg.name = "tui_devpc";
	cfg.url = "http://127.0.0.1:7820";
	cfg.tier = a2a_server_tier::ephemeral_runtime;
	a2a_server_manager::get_instance().add_server(cfg);

	std::cout << "Testing create_a2a_servers_dialog and create_a2a_server_edit_dialog..." << std::endl;
	{
		auto dlg = create_a2a_servers_dialog(0);
		assert(dlg != nullptr);
		assert(dlg->get_title() == "A2A Remote Servers");

		auto add_dlg = create_a2a_server_edit_dialog("", "http://localhost:8000");
		assert(add_dlg != nullptr);
		assert(add_dlg->get_title() == "Add A2A Server");

		auto modify_dlg = create_a2a_server_edit_dialog("tui_devpc", "http://127.0.0.1:7820", "secret_token");
		assert(modify_dlg != nullptr);
		assert(modify_dlg->get_title() == "Edit A2A Server");
		assert(modify_dlg->get_value("name").value_or("") == "tui_devpc");
		assert(modify_dlg->get_value("url").value_or("") == "http://127.0.0.1:7820");
		assert(modify_dlg->get_value("auth").value_or("") == "secret_token");
	}

	std::cout << "A2A TUI Dialog creation verified successfully!" << std::endl;
	return 0;
}
