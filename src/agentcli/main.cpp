#include <cstdlib>
#include <iostream>
#include <memory>
#include <CLI11.hpp>
#include "../agentlib/ai_agent.h"
#include "../agentlib/httplib_transport.h"
#include "../agentlib/llm_client.h"
#include "../agentlib/recording_transport.h"
#include "../agentlib/replay_transport.h"
#include "../agentlib/skill_manager.h"
#include "../agentlib/tool_registry.h"
#include "../event_queue.h"

#include "../agentlib/ai_model.h"

using namespace agentlib;
using json = nlohmann::json;

int main(int argc, char **argv)
{
	CLI::App app{"Turbostar Agent CLI for E2E Testing"};
	
	std::string prompt = "How cold is it outside in San Francisco, CA?";
	std::string replay_file = "tests/data/todo_traffic.json";
	std::string dump_state_file = "";
	std::string project_dir = "";
	long long mock_epoch = 0;

	app.add_option("-p,--prompt", prompt, "The initial user prompt to send to the agent");
	app.add_option("-r,--replay", replay_file, "Traffic file for replay/record modes");
	app.add_option("--dump-state", dump_state_file, "Dump the final conversation state to this JSON file before exiting");
	app.add_option("--project-dir", project_dir, "Override the project root directory for isolated sandboxing");
	app.add_option("--mock-epoch", mock_epoch, "Force a deterministic timestamp for milestone archives");

	CLI11_PARSE(app, argc, argv);

	if (!project_dir.empty()) {
		setenv("TURBOSTAR_TEST_PROJECT_DIR", project_dir.c_str(), 1);
	}
	if (mock_epoch > 0) {
		setenv("TURBOSTAR_TEST_MOCK_EPOCH", std::to_string(mock_epoch).c_str(), 1);
	}

	const char *env_url = std::getenv("LLM_URL");
	std::string url = env_url ? env_url : "http://192.168.1.55:8080";

	// Set up the transport chain
#if defined(LLM_TRANSPORT_REPLAY)
	std::cout << "[Using Replay Transport]" << std::endl;
	auto player = std::make_shared<replay_transport>(replay_file);
	llm_client client(player, "gpt-4o");

#elif defined(LLM_TRANSPORT_RECORD)
	std::cout << "[Using Recording Transport]" << std::endl;
	auto http_transport = std::make_shared<httplib_transport>(url);
	auto recorder = std::make_shared<recording_transport>(http_transport, replay_file);
	llm_client client(recorder, "gpt-4o");
#else
	std::cout << "[Using Standard HTTP Transport]" << std::endl;
	auto http_transport = std::make_shared<httplib_transport>(url);
	llm_client client(http_transport, "gpt-4o");
#endif

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;
	auto model = std::make_shared<ai_model>("cli-model", "CLI Model", url, "CLI Test", 0.0, 0.0);
	auto test_agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = test_agent.get();

	agentlib::skill_manager::get_instance().initialize();
	ctx.fs_security.set_working_directory(std::filesystem::current_path());
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::read);
	ctx.fs_security.add_allowed_root(std::filesystem::current_path(), access_type::write);
	ctx.fs_security.set_vfs(agentlib::skill_manager::get_instance().get_vfs());

	std::cout << "Connecting to: " << url << std::endl;
	std::cout << "Prompt: " << prompt << "\n" << std::endl;

	std::vector<message> conversation;

	message system_msg;
	system_msg.role = "system";
	system_msg.content = "You are a helpful assistant. You must use the provided tools to complete tasks.";
	conversation.push_back(system_msg);

	message user_msg;
	user_msg.role = "user";
	user_msg.content = prompt;
	conversation.push_back(user_msg);

	while (true) {
		agentlib::llm_chat_response chat_res = client.send_chat(conversation, &registry);
		message response = chat_res.msg;
		
		// If the response is completely empty (often happens in E2E replays if the transport is exhausted)
		if (response.content.empty() && (!response.tool_calls || response.tool_calls->empty())) {
			break;
		}

		if (response.tool_calls && !response.tool_calls->empty()) {
			std::cout << "LLM requested tool calls." << std::endl;
			// The LLM's assistant message needs to be added to the history
			conversation.push_back(response);

			for (const auto &call : *response.tool_calls) {
				std::cout << "Executing tool: " << call.function.name << std::endl;

				// Sync state to agent before execution (so tools can modify it)
				test_agent->set_conversation(conversation);

				std::string tool_result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
				
				// Sync state back from agent (in case it paged out)
				conversation = test_agent->get_conversation();

				std::cout << "[Tool Result] " << tool_result << std::endl;

				message tool_msg;
				tool_msg.role = "tool";
				tool_msg.content = tool_result;
				tool_msg.name = call.function.name;
				tool_msg.tool_call_id = call.id;

				conversation.push_back(tool_msg);
			}
			
			// Compact ephemeral errors
			test_agent->set_conversation(conversation);
			test_agent->compact_ephemeral_errors(conversation);
			// compact_ephemeral_errors mutates BOTH the passed array and the internal array.
			// But just to be safe, sync back.
			conversation = test_agent->get_conversation();
			
		} else {
			std::cout << "\nLLM Final Response:\n" << response.content << std::endl;
			break;
		}
	}

	if (!dump_state_file.empty()) {
		nlohmann::json root;
		root["stats"] = test_agent->get_stats();
		
		nlohmann::json conv_array = nlohmann::json::array();
		for (const auto& msg : conversation) {
			nlohmann::json m_json;
			to_json(m_json, msg);
			conv_array.push_back(m_json);
		}
		root["conversation"] = conv_array;

		std::ofstream file(dump_state_file);
		if (file.is_open()) {
			file << root.dump(4);
			std::cout << "Dumped final state to " << dump_state_file << std::endl;
		}
	}

	return 0;
}
