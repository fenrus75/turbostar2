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

		auto edit_dlg = create_a2a_server_edit_dialog("new_node", "http://localhost:8000");
		assert(edit_dlg != nullptr);
		assert(edit_dlg->get_title() == "Add A2A Server");
	}

	std::cout << "A2A TUI Dialog creation verified successfully!" << std::endl;
	return 0;
}
