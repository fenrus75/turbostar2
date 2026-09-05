// Tested source file: src/mcp/turbomcp_server.cpp
#include "mcp/turbomcp_server.h"
#include "agentlib/tool_registry.h"
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
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

	// Verify that turbomcp_server retains tool_context across consecutive calls
	{
		agentlib::turbomcp_server server;
		assert(server.get_context().edit_sequence_counter == 0);

		// Call an edit tool that updates file health state, or simulate sequence advancement
		nlohmann::json edit_req1 = {
			{"jsonrpc", "2.0"},
			{"id", 1},
			{"method", "tools/call"},
			{"params", {
				{"name", "fs_file_size"},
				{"arguments", {{"path", "src/main.cpp"}}}
			}}
		};
		server.handle_request(edit_req1);

		// Now test that if an edit occurs, edit_sequence_counter increments and is retained
		nlohmann::json edit_req2 = {
			{"jsonrpc", "2.0"},
			{"id", 2},
			{"method", "tools/call"},
			{"params", {
				{"name", "fs_replace_content"},
				{"arguments", {
					{"target_file", "src/main.cpp"},
					{"target_content", "nonexistent_target_to_fail_cleanly"},
					{"replacement_content", "foo"}
				}}
			}}
		};
		server.handle_request(edit_req2);

		// Sequence counter should increment on successful edit tool invocation
		// Let's create a temporary file to do 2 actual successful edits
		std::string tmp_file = "test_mcp_seq.tmp";
		{
			std::ofstream ofs(tmp_file);
			ofs << "line 1\nline 2\nline 3\n";
		}

		nlohmann::json edit1 = {
			{"jsonrpc", "2.0"},
			{"id", 3},
			{"method", "tools/call"},
			{"params", {
				{"name", "fs_replace_content"},
				{"arguments", {
					{"path", tmp_file},
					{"target_content", "line 1"},
					{"replacement_content", "line 1 modified"}
				}}
			}}
		};
		nlohmann::json resp1 = server.handle_request(edit1);
		std::cout << "MCP edit 1 response: " << resp1.dump() << std::endl;
		assert(server.get_context().edit_sequence_counter == 1);
		assert(resp1["result"]["content"][0]["text"].get<std::string>().find("[Edit ID: #1]") != std::string::npos);

		nlohmann::json edit2 = {
			{"jsonrpc", "2.0"},
			{"id", 4},
			{"method", "tools/call"},
			{"params", {
				{"name", "fs_replace_content"},
				{"arguments", {
					{"path", tmp_file},
					{"target_content", "line 2"},
					{"replacement_content", "line 2 modified"}
				}}
			}}
		};
		nlohmann::json resp2 = server.handle_request(edit2);
		std::cout << "MCP edit 2 response: " << resp2.dump() << std::endl;
		assert(server.get_context().edit_sequence_counter == 2);
		assert(resp2["result"]["content"][0]["text"].get<std::string>().find("[Edit ID: #2]") != std::string::npos);

		std::filesystem::remove(tmp_file);
	}

	std::cout << "test_turbomcp_server passed!" << std::endl;
	return 0;
}
