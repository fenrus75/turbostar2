// Tested source file: src/mcp/turbomcp_server.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "agentlib/tool_registry.h"
#include "mcp/turbomcp_server.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog();

	// Verify that tool validators correctly report expose_in_mcp()
	auto validators = agentlib::tool_registry::get_instance().get_all_registered_validators();
	bool found_open_in_editor = false;
	bool found_fs_read_binary = false;
	bool found_perform_code_review = false;

	for (const auto &val : validators) {
		if (!val)
			continue;
		if (val->get_name() == "open_in_editor") {
			found_open_in_editor = true;
			assert(val->expose_in_mcp() == false);
		}
		if (val->get_name() == "fs_read_binary") {
			found_fs_read_binary = true;
			assert(val->expose_in_mcp() == true);
		}
		if (val->get_name() == "perform_code_review") {
			found_perform_code_review = true;
			assert(val->expose_in_mcp() == false);
		}
	}

	assert(found_open_in_editor);
	assert(found_fs_read_binary);
	assert(found_perform_code_review);

	// Verify that turbomcp_server retains tool_context across consecutive calls
	{
		agentlib::turbomcp_server server;
		assert(server.get_context().edit_sequence_counter == 0);

		// Verify tools/list does not include perform_code_review or open_in_editor
		nlohmann::json list_req = {
		    {"jsonrpc", "2.0"}, {"id", "list-1"}, {"method", "tools/list"}, {"params", nlohmann::json::object()}};
		nlohmann::json list_resp = server.handle_request(list_req);
		bool list_has_perform_code_review = false;
		bool list_has_open_in_editor = false;
		for (const auto &tool : list_resp["result"]["tools"]) {
			if (tool["name"] == "perform_code_review")
				list_has_perform_code_review = true;
			if (tool["name"] == "open_in_editor")
				list_has_open_in_editor = true;
		}
		assert(!list_has_perform_code_review);
		assert(!list_has_open_in_editor);

		// Strict standards compliance review on tools/list inputSchema:
		// Ensure every tool returns a valid JSON Schema object complying with MCP spec.
		for (const auto &tool : list_resp["result"]["tools"]) {
			assert(tool.contains("name") && tool["name"].is_string() && !tool["name"].get<std::string>().empty());
			assert(tool.contains("inputSchema") && tool["inputSchema"].is_object());
			const auto &schema = tool["inputSchema"];
			assert(schema.contains("type") && schema["type"] == "object");

			if (schema.contains("properties")) {
				assert(schema["properties"].is_object());
				for (auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it) {
					// Property must be an object (not an array of pairs from mismatched braces!)
					assert(it.value().is_object());
					if (it.value().contains("type")) {
						assert(it.value()["type"].is_string() || it.value()["type"].is_array());
						if (it.value()["type"] == "array" && it.value().contains("items")) {
							assert(it.value()["items"].is_object() || it.value()["items"].is_array());
							if (it.value()["items"].is_object() && it.value()["items"].contains("properties")) {
								assert(it.value()["items"]["properties"].is_object());
							}
						}
					}
					if (it.value().contains("enum")) {
						assert(it.value()["enum"].is_array());
					}
				}
			}
			if (schema.contains("required")) {
				assert(schema["required"].is_array());
				for (const auto &req_item : schema["required"]) {
					assert(req_item.is_string());
				}
			}
		}

		// Calling an unexposed tool via tools/call returns an error
		nlohmann::json unexposed_call = {
		    {"jsonrpc", "2.0"},
		    {"id", "call-unexposed"},
		    {"method", "tools/call"},
		    {"params", {{"name", "perform_code_review"}, {"arguments", {{"files", nlohmann::json::array({"src/main.cpp"})}}}}}};
		nlohmann::json unexposed_resp = server.handle_request(unexposed_call);
		assert(unexposed_resp["result"]["isError"] == true);

		// Call an edit tool that updates file health state, or simulate sequence advancement
		nlohmann::json edit_req1 = {{"jsonrpc", "2.0"},
					    {"id", 1},
					    {"method", "tools/call"},
					    {"params", {{"name", "fs_file_size"}, {"arguments", {{"path", "src/main.cpp"}}}}}};
		server.handle_request(edit_req1);

		// Now test that if an edit occurs, edit_sequence_counter increments and is retained
		nlohmann::json edit_req2 = {{"jsonrpc", "2.0"},
					    {"id", 2},
					    {"method", "tools/call"},
					    {"params",
					     {{"name", "fs_replace_content"},
					      {"arguments",
					       {{"target_file", "src/main.cpp"},
						{"target_content", "nonexistent_target_to_fail_cleanly"},
						{"replacement_content", "foo"}}}}}};
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
		    {"params",
		     {{"name", "fs_replace_content"},
		      {"arguments", {{"path", tmp_file}, {"target_content", "line 1"}, {"replacement_content", "line 1 modified"}}}}}};
		nlohmann::json resp1 = server.handle_request(edit1);
		std::cout << "MCP edit 1 response: " << resp1.dump() << std::endl;
		assert(server.get_context().edit_sequence_counter == 1);
		assert(resp1["result"]["content"][0]["text"].get<std::string>().find("[Edit ID: #1]") != std::string::npos);

		nlohmann::json edit2 = {
		    {"jsonrpc", "2.0"},
		    {"id", 4},
		    {"method", "tools/call"},
		    {"params",
		     {{"name", "fs_replace_content"},
		      {"arguments", {{"path", tmp_file}, {"target_content", "line 2"}, {"replacement_content", "line 2 modified"}}}}}};
		nlohmann::json resp2 = server.handle_request(edit2);
		std::cout << "MCP edit 2 response: " << resp2.dump() << std::endl;
		assert(server.get_context().edit_sequence_counter == 2);
		assert(resp2["result"]["content"][0]["text"].get<std::string>().find("[Edit ID: #2]") != std::string::npos);

		std::filesystem::remove(tmp_file);

		// Verify MCP tool parameter identities (turbostar snake_case, antigravity PascalCase, copilot camelCase)
		assert(server.get_tool_identity() == agentlib::mcp_tool_identity::turbostar);

		auto find_tool_schema = [](const nlohmann::json &list_resp_json, const std::string &name) -> nlohmann::json {
			for (const auto &tool : list_resp_json["result"]["tools"]) {
				if (tool["name"] == name) {
					return tool["inputSchema"];
				}
			}
			return nlohmann::json::object();
		};

		// 1. Auto-detect "antigravity" from initialize clientInfo
		nlohmann::json init_agy = {{"jsonrpc", "2.0"},
					   {"id", "init-1"},
					   {"method", "initialize"},
					   {"params", {{"clientInfo", {{"name", "antigravity-cli"}, {"version", "2.0"}}}}}};
		server.handle_request(init_agy);
		assert(server.get_tool_identity() == agentlib::mcp_tool_identity::antigravity);

		nlohmann::json list_agy = server.handle_request(list_req);
		nlohmann::json read_schema_agy = find_tool_schema(list_agy, "fs_read_lines");
		assert(read_schema_agy["properties"].contains("Path"));
		assert(read_schema_agy["properties"].contains("StartLine"));
		assert(read_schema_agy["properties"].contains("EndLine"));
		assert(!read_schema_agy["properties"].contains("start_line"));
		assert(read_schema_agy["required"][0] == "Path");

		// Verify descriptions inside fs_read_lines are rewritten to PascalCase for parameter references
		// while preserving normal English prose ("lines", "start", "end").
		std::string len_desc_agy = read_schema_agy["properties"]["Length"]["description"].get<std::string>();
		assert(len_desc_agy.find("'StartLine'") != std::string::npos);
		assert(len_desc_agy.find("'EndLine'") != std::string::npos);
		assert(len_desc_agy.find("'Tail'") != std::string::npos);
		assert(len_desc_agy.find("number of lines to read") != std::string::npos);

		std::string tail_desc_agy = read_schema_agy["properties"]["Tail"]["description"].get<std::string>();
		assert(tail_desc_agy.find("'StartLine'") != std::string::npos);
		assert(tail_desc_agy.find("'EndLine'") != std::string::npos);
		assert(tail_desc_agy.find("'Length'") != std::string::npos);
		assert(tail_desc_agy.find("from the end of the file") != std::string::npos);

		// Verify slash-delimited parameter references in fs_batch_read ("start_line/end_line/length" -> "StartLine/EndLine/Length")
		// while preserving file path examples ("src/mime.h").
		nlohmann::json batch_schema_agy = find_tool_schema(list_agy, "fs_batch_read");
		assert(batch_schema_agy["properties"]["Items"]["items"]["properties"].contains("StartLine"));
		assert(batch_schema_agy["properties"]["Items"]["items"]["properties"].contains("EndLine"));
		std::string batch_tail_desc =
			batch_schema_agy["properties"]["Items"]["items"]["properties"]["Tail"]["description"].get<std::string>();
		assert(batch_tail_desc.find("StartLine/EndLine/Length") != std::string::npos);
		std::string batch_path_desc =
			batch_schema_agy["properties"]["Items"]["items"]["properties"]["Path"]["description"].get<std::string>();
		assert(batch_path_desc.find("src/mime.h") != std::string::npos);

		// Also verify single-word slash-delimited parameter list ("start/end/length")
		std::vector<std::pair<std::string, std::string>> single_word_renames = {
			{"start", "Start"}, {"end", "End"}, {"length", "Length"}};
		assert(agentlib::turbomcp_server::transform_description_for_identity(
			       "Mutually exclusive with start/end/length in src/mime.h.", single_word_renames) ==
		       "Mutually exclusive with Start/End/Length in src/mime.h.");

		nlohmann::json multi_schema_agy = find_tool_schema(list_agy, "fs_multi_replace_content");
		assert(multi_schema_agy["properties"].contains("Chunks"));
		assert(multi_schema_agy["properties"]["Chunks"]["items"]["properties"].contains("TargetContent"));
		assert(multi_schema_agy["properties"]["Chunks"]["items"]["properties"].contains("ReplacementContent"));

		// 2. Auto-detect "copilot" from initialize clientInfo
		nlohmann::json init_cop = {{"jsonrpc", "2.0"},
					   {"id", "init-2"},
					   {"method", "initialize"},
					   {"params", {{"clientInfo", {{"name", "Visual Studio Code - Copilot"}, {"version", "1.0"}}}}}};
		server.handle_request(init_cop);
		assert(server.get_tool_identity() == agentlib::mcp_tool_identity::copilot);

		nlohmann::json list_cop = server.handle_request(list_req);
		nlohmann::json read_schema_cop = find_tool_schema(list_cop, "fs_read_lines");
		assert(read_schema_cop["properties"].contains("path"));
		assert(read_schema_cop["properties"].contains("startLine"));
		assert(read_schema_cop["properties"].contains("endLine"));
		assert(!read_schema_cop["properties"].contains("start_line"));
		assert(read_schema_cop["required"][0] == "path");

		std::string len_desc_cop = read_schema_cop["properties"]["length"]["description"].get<std::string>();
		assert(len_desc_cop.find("'startLine'") != std::string::npos);
		assert(len_desc_cop.find("'endLine'") != std::string::npos);
		assert(len_desc_cop.find("'tail'") != std::string::npos);

		nlohmann::json multi_schema_cop = find_tool_schema(list_cop, "fs_multi_replace_content");
		assert(multi_schema_cop["properties"].contains("chunks"));
		assert(multi_schema_cop["properties"]["chunks"]["items"]["properties"].contains("targetContent"));
		assert(multi_schema_cop["properties"]["chunks"]["items"]["properties"].contains("replacementContent"));

		// 3. Explicit set_tool_identity override takes precedence over subsequent initialize
		assert(server.set_tool_identity("turbostar"));
		assert(server.get_tool_identity() == agentlib::mcp_tool_identity::turbostar);
		server.handle_request(init_agy);
		assert(server.get_tool_identity() == agentlib::mcp_tool_identity::turbostar);

		// 4. Verify tools/call accepts PascalCase, camelCase, and snake_case regardless of active identity
		server.set_tool_identity(agentlib::mcp_tool_identity::antigravity);
		for (const auto &arg_key : {"Path", "AbsolutePath", "filePath", "path"}) {
			nlohmann::json call_req = {{"jsonrpc", "2.0"},
						   {"id", "call-size"},
						   {"method", "tools/call"},
						   {"params", {{"name", "fs_file_size"}, {"arguments", {{arg_key, "src/main.cpp"}}}}}};
			nlohmann::json call_resp = server.handle_request(call_req);
			assert(!call_resp["result"].value("isError", false));
		}
	}

	std::cout << "test_turbomcp_server passed!" << std::endl;
	return 0;
}
