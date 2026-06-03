#include <cstdlib>
#include <iostream>
#include <memory>
#include <unordered_map>
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
#include "../config_manager.h"

using namespace agentlib;
using json = nlohmann::json;

int main(int argc, char **argv)
{
	CLI::App app{"Turbostar Agent CLI for E2E Testing"};
	
	std::string prompt = "How cold is it outside in San Francisco, CA?";
	std::string replay_file = "tests/data/todo_traffic.json";
	std::string dump_state_file = "";
	std::string project_dir = "";

	app.add_option("prompt,-p,--prompt", prompt, "The initial user prompt to send to the agent");
	app.add_option("replay,-r,--replay", replay_file, "Traffic file for replay/record modes");
	app.add_option("--dump-state", dump_state_file, "Dump the final conversation state to this JSON file before exiting");
	app.add_option("--project-dir", project_dir, "Override the project root directory for isolated sandboxing");

	CLI11_PARSE(app, argc, argv);

	{
		if (!project_dir.empty()) {
			setenv("TURBOSTAR_TEST_PROJECT_DIR", project_dir.c_str(), 1);
		}

	const char *env_url = std::getenv("LLM_URL");
	std::string url = env_url ? env_url : "http://192.168.1.55:8080";

	// Load configuration to get the default model from the inventory
	config_manager::get_instance().load();

	std::string default_model_id = "gpt-4o";
	api_type default_type = api_type::openai;
	std::string default_name = "GPT-4o";
	double cost_tx = 5.0;
	double cost_rx = 15.0;
	std::string api_key = "";
	model_cost_type cost_type = model_cost_type::paid_per_token;

	std::string cfg_model_id = config_manager::get_instance().get_default_model_id();
	auto registry_model = ai_model_registry::get_instance().get_model(cfg_model_id);
	if (registry_model) {
		default_model_id = registry_model->get_id();
		default_type = registry_model->get_api_type();
		default_name = registry_model->get_name();
		cost_tx = registry_model->get_cost_per_1m_tx();
		cost_rx = registry_model->get_cost_per_1m_rx();
		api_key = registry_model->get_api_key();
		cost_type = registry_model->get_cost_type();
	}

	// Set up the transport chain using the resolved default model details
#if defined(LLM_TRANSPORT_REPLAY)
	std::cout << "[Using Replay Transport]" << std::endl;
	auto player = std::make_shared<replay_transport>(replay_file);
	llm_client client(player, default_model_id, default_type);

#elif defined(LLM_TRANSPORT_RECORD)
	std::cout << "[Using Recording Transport]" << std::endl;
	auto http_transport = std::make_shared<httplib_transport>(url, api_key);
	auto recorder = std::make_shared<recording_transport>(http_transport, replay_file);
	llm_client client(recorder, default_model_id, default_type);
#else
	std::cout << "[Using Standard HTTP Transport]" << std::endl;
	auto http_transport = std::make_shared<httplib_transport>(url, api_key);
	llm_client client(http_transport, default_model_id, default_type);
#endif

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;
	auto model = std::make_shared<ai_model>(default_model_id, default_name, url, "CLI Test", cost_tx, cost_rx, api_key, default_type, 250000, cost_type);
	auto test_agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = test_agent.get();

	agentlib::skill_manager::get_instance().initialize();
	
	std::filesystem::path workspace_root = project_dir.empty() ? std::filesystem::current_path() : std::filesystem::path(project_dir);
	ctx.fs_security.set_working_directory(workspace_root);
	ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
	ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
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
			for (auto &call : *response.tool_calls) {
				agentlib::normalize_tool_call(call);
			}
			std::unordered_map<std::string, std::string> merged_to_parent;
			std::unordered_map<std::string, std::pair<int, int>> parent_ranges;
			ai_agent::coalesce_tool_calls(*response.tool_calls, merged_to_parent, parent_ranges);

			std::cout << "LLM requested tool calls." << std::endl;
			// The LLM's assistant message needs to be added to the history
			conversation.push_back(response);

			for (const auto &call : *response.tool_calls) {
				std::cout << "Executing tool: " << call.function.name << std::endl;

				// Sync state to agent before execution (so tools can modify it)
				test_agent->set_conversation(conversation);

				std::string tool_result;
				bool is_merged = (merged_to_parent.find(call.id) != merged_to_parent.end());
				if (is_merged) {
					std::string parent_id = merged_to_parent[call.id];
					int p_start = 1;
					int p_end = 1000000;
					if (parent_ranges.find(parent_id) != parent_ranges.end()) {
						p_start = parent_ranges[parent_id].first;
						p_end = parent_ranges[parent_id].second;
					}
					tool_result = std::format("Note: This read request was adjacent to/overlapping with another read request in the same turn. "
								  "To avoid redundant output and keep the code contiguous, it has been merged into tool call {} "
								  "(which reads lines {} - {}). Please refer to the output of that tool call for the content.",
								  parent_id, p_start, p_end);
				} else {
					tool_result = registry.execute_tool(call.function.name, call.function.arguments, ctx);
				}
				
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
			
			// Append response to conversation so auto-episode evaluates it
			conversation.push_back(response);
			test_agent->set_conversation(conversation);
			test_agent->evaluate_auto_episode(conversation);
			
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
	}

	std::_Exit(0);
}
