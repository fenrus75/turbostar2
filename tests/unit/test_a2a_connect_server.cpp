#include <cassert>
#include <iostream>
#include "a2a/a2a_server.h"
#include "a2a/a2a_server_manager.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/tool_registry.h"
#include "project_manager.h"
#include "test_watchdog.h"
#include "tools/a2a_connect_server/a2a_connect_server.h"

using namespace agentlib;
using namespace a2a;

int main()
{
	test_watchdog::setup_watchdog(30);

	std::filesystem::path temp_home = std::filesystem::absolute("./test_a2a_conn_home");
	if (std::filesystem::exists(temp_home)) {
		std::filesystem::remove_all(temp_home);
	}
	std::filesystem::create_directories(temp_home);
	setenv("HOME", temp_home.c_str(), 1);

	project_manager::get_instance().initialize();
	subagent_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	int bound_port = 0;
	bool started = a2a_server::get_instance().start(7870, &bound_port);
	assert(started);

	std::cout << "Testing a2a_connect_server tool..." << std::endl;
	{
		std::string url = std::format("http://127.0.0.1:{}", bound_port);

		// 1. Success case: connect to local server
		std::string args = std::format("{{\"name\": \"test_node\", \"url\": \"{}\"}}", url);
		std::string res = registry.execute_tool("a2a_connect_server", args, ctx);
		std::cout << "Connect result: " << res << std::endl;
		assert(res.find("Successfully connected") != std::string::npos);

		auto found = a2a_server_manager::get_instance().find_server("test_node");
		assert(found.has_value());
		assert(found->url == url);

		// 2. Reject empty name
		{
			auto prep = registry.prepare_tool("a2a_connect_server", "{\"name\": \"\", \"url\": \"http://localhost\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("cannot be empty") != std::string::npos);
		}

		// 3. Reject invalid server name with colons or slashes
		{
			auto prep = registry.prepare_tool("a2a_connect_server", "{\"name\": \"invalid:name\", \"url\": \"http://localhost\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("contain colons or slashes") != std::string::npos);
		}

		a2a_server::get_instance().stop();
		std::cout << "a2a_connect_server verified successfully!" << std::endl;
	}

	return 0;
}
