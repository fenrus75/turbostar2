// Tested source file: src/mcp/turbomcp_server.cpp
#include "mcp/turbomcp_server.h"
#include "agentlib/tool_registry.h"
#include "test_watchdog.h"
#include <cassert>
#include <iostream>

int main()
{
	test_watchdog::setup_watchdog();

	// Verify that tool validators correctly report expose_in_mcp()
	auto validators = agentlib::tool_registry::get_instance().get_all_registered_validators();
	bool found_open_in_editor = false;
	bool found_fs_read_binary = false;

	for (const auto &val : validators) {
		if (!val) continue;
		if (val->get_name() == "open_in_editor") {
			found_open_in_editor = true;
			assert(val->expose_in_mcp() == false);
		}
		if (val->get_name() == "fs_read_binary") {
			found_fs_read_binary = true;
			assert(val->expose_in_mcp() == true);
		}
	}

	assert(found_open_in_editor);
	assert(found_fs_read_binary);

	std::cout << "test_turbomcp_server passed!" << std::endl;
	return 0;
}
