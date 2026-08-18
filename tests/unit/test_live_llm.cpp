// Tested source file: src/agentlib/protocols/openai_response_connection.h, src/agentlib/httplib_transport.cpp
#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include "../../src/agentlib/httplib_transport.h"
#include "../../src/agentlib/ai_model.h"
#include "../../src/agentlib/llm_client.h"
#include "../../src/agentlib/llm_types.h"


#include <CLI11.hpp>

#ifndef LIVE_LLM_TESTS_ENABLED
#define LIVE_LLM_TESTS_ENABLED 0
#endif

#ifndef LIVE_LLM_URL
#define LIVE_LLM_URL "http://localhost:8000/v1"
#endif

#ifndef LIVE_LLM_MODEL
#define LIVE_LLM_MODEL "default"
#endif

int main(int argc, char **argv)
{
	test_watchdog::setup_watchdog(45);

	CLI::App app{"Turbostar Live LLM Test Tool Harness"};
	std::string tool_name;
	std::string prompt_str;
	std::string expect_str;

	app.add_option("--tool", tool_name, "Tool name under test");
	app.add_option("--prompt", prompt_str, "Test prompt sent to the LLM");
	app.add_option("--expect", expect_str, "Expected substring in tool output");
	CLI11_PARSE(app, argc, argv);


	std::string server_url = LIVE_LLM_URL;
	const char *env_url = std::getenv("TURBOSTAR_LIVE_LLM_URL");
	if (!env_url || !*env_url) {
		env_url = std::getenv("OPENAI_API_BASE");
	}
	if (env_url && *env_url) {
		server_url = env_url;
	}

	const char *env_enabled = std::getenv("TURBOSTAR_ENABLE_LIVE_LLM_TESTS");
	bool enabled = (LIVE_LLM_TESTS_ENABLED != 0) || (env_enabled && std::string(env_enabled) == "1") || (env_url != nullptr);

	if (!enabled) {
		std::cout << "[SKIPPED] Live LLM tests disabled. Enable via Meson option (-Dlive-llm-tests=true) or env (TURBOSTAR_LIVE_LLM_URL).\n";
		return 77; // Meson skip exit code
	}

	std::cout << "Testing live LLM server connection at: " << server_url << std::endl;

	std::string error_out;
	auto models = agentlib::fetch_models_from_server(server_url, error_out);


	if (models.empty()) {
		std::cout << "[SKIPPED] Live LLM server at " << server_url << " returned no models or is unreachable: " << error_out << std::endl;
		return 77; // Meson skip exit code
	}

	std::cout << "Successfully connected to live LLM server! Available models:\n";
	for (const auto &m : models) {
		std::cout << "  - " << m->get_name() << " (" << m->get_id() << ")\n";
	}

	assert(!models.empty());
	std::string target_model = models.front()->get_id();

	std::cout << "Sending live completion query to model: " << target_model << std::endl;

	auto transport = std::make_shared<agentlib::httplib_transport>(server_url);
	agentlib::llm_client client(transport, target_model, agentlib::api_type::openai);

	std::vector<agentlib::message> convo;
	agentlib::message user_msg;
	user_msg.role = "user";
	user_msg.content = "Hello! Please reply with the single word: PONG";
	convo.push_back(user_msg);

	auto response = client.send_chat(convo);

	std::cout << "Received response from model:\n" << response.msg.content << std::endl;
	assert(!response.msg.content.empty());

	std::cout << "Live LLM completion test passed successfully!\n";

	// Test 2: Live Tool Calling Loop
	std::string test_target_tool = tool_name.empty() ? "run_python" : tool_name;
	std::string test_prompt = prompt_str.empty() ? "Please run the run_python tool to execute python code calculating 2468 * 1357." : prompt_str;
	std::string test_expect = expect_str.empty() ? "3349076" : expect_str;

	std::cout << "\n--- Testing Live Tool Calling Loop (" << test_target_tool << ") ---\n";

	auto &registry = agentlib::tool_registry::get_instance();
	agentlib::tool_context ctx;
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::read);
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), agentlib::access_type::write);

	std::vector<agentlib::message> tool_convo;
	agentlib::message sys_msg;
	sys_msg.role = "system";
	sys_msg.content = "You are a software testing agent. When requested to run a tool, you MUST issue a native function call / tool call or format json tool call block. Do NOT return raw explanations without tool calls.";
	tool_convo.push_back(sys_msg);

	agentlib::message tool_user_msg;
	tool_user_msg.role = "user";
	tool_user_msg.content = test_prompt;
	tool_convo.push_back(tool_user_msg);

	auto tool_response = client.send_chat(tool_convo, &registry);

	std::vector<agentlib::tool_call> calls_to_exec;
	if (tool_response.msg.tool_calls && !tool_response.msg.tool_calls->empty()) {
		calls_to_exec = *tool_response.msg.tool_calls;
	} else {
		// Parse pseudo tool call block from content (either raw JSON or Markdown json block)
		std::string json_str;
		size_t json_start = tool_response.msg.content.find("```json");
		if (json_start != std::string::npos) {
			size_t body_start = tool_response.msg.content.find('{', json_start);
			size_t body_end = tool_response.msg.content.find("```", body_start);
			if (body_start != std::string::npos && body_end != std::string::npos) {
				json_str = tool_response.msg.content.substr(body_start, body_end - body_start);
			}
		} else {
			size_t body_start = tool_response.msg.content.find('{');
			size_t body_end = tool_response.msg.content.rfind('}');
			if (body_start != std::string::npos && body_end != std::string::npos && body_end > body_start) {
				json_str = tool_response.msg.content.substr(body_start, body_end - body_start + 1);
			}
		}

		if (!json_str.empty()) {
			try {
				nlohmann::json parsed = nlohmann::json::parse(json_str);
				if (parsed.contains("name") && parsed.contains("arguments")) {
					agentlib::tool_call tc;
					tc.id = "call_pseudo_1";
					tc.function.name = parsed["name"].get<std::string>();
					if (parsed["arguments"].is_string()) {
						tc.function.arguments = parsed["arguments"].get<std::string>();
					} else {
						tc.function.arguments = parsed["arguments"].dump();
					}
					calls_to_exec.push_back(tc);
				}
			} catch (...) {
			}
		}
	}

	if (!calls_to_exec.empty()) {
		std::cout << "Detected " << calls_to_exec.size() << " tool call(s) from model:\n";
		tool_convo.push_back(tool_response.msg);

		for (const auto &call : calls_to_exec) {
			std::cout << "  Tool: " << call.function.name << " Args: " << call.function.arguments << std::endl;
			std::string result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
			std::cout << "  Result:\n" << result << std::endl;

			if (!test_expect.empty()) {
				assert(result.find(test_expect) != std::string::npos);
			}

			agentlib::message tool_ret_msg;
			tool_ret_msg.role = "tool";
			tool_ret_msg.content = result;
			tool_ret_msg.tool_call_id = call.id;
			tool_convo.push_back(tool_ret_msg);
		}

		auto final_response = client.send_chat(tool_convo, &registry);
		std::cout << "Final response from model:\n" << final_response.msg.content << std::endl;
		assert(!final_response.msg.content.empty());
		std::cout << "Live tool calling test (" << test_target_tool << ") passed successfully!\n";
	} else {
		std::cout << "Model response without tool_calls:\n" << tool_response.msg.content << std::endl;
	}

	return 0;
}





