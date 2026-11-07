#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>
#include <CLI11.hpp>
#include "agentlib/agent_subsystems.h"
#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
#include "agentlib/httplib_transport.h"
#include "agentlib/llm_client.h"
#include "agentlib/recording_transport.h"
#include "agentlib/replay_transport.h"
#include "agentlib/skill_manager.h"
#include "agentlib/subagent_manager.h"
#include "agentlib/tool_registry.h"
#include "agentlib/tool_tracer.h"
#include "config_manager.h"
#include "event_queue.h"
#include "hex/hex_highlighter_registry.h"
#include "images/image_manager.h"
#include "mapped_file.h"
#include "pluginloader.h"
#include "project_manager.h"

using namespace agentlib;
using json = nlohmann::json;

namespace
{

struct parsed_toolcall_file {
	std::string source_file;
	std::string name;
	std::string args_json;
};

static bool parse_toolcall_trace_file(const std::string &path, parsed_toolcall_file &out, std::string &out_err)
{
	std::ifstream in(path);
	if (!in.is_open()) {
		out_err = std::format("Could not open toolcall file: {}", path);
		return false;
	}
	std::string untrusted_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	size_t sep = untrusted_content.find("\n---\n");
	std::string untrusted_header = (sep != std::string::npos) ? untrusted_content.substr(0, sep) : untrusted_content;

	try {
		auto j = json::parse(untrusted_header);
		if (!j.is_object() || !j.contains("name") || !j["name"].is_string()) {
			out_err = std::format("Invalid toolcall header in {}: missing string 'name'", path);
			return false;
		}
		out.source_file = path;
		out.name = j["name"].get<std::string>();
		if (j.contains("arguments")) {
			if (j["arguments"].is_string()) {
				out.args_json = j["arguments"].get<std::string>();
			} else {
				out.args_json = j["arguments"].dump();
			}
		} else {
			out.args_json = "{}";
		}
		return true;
	} catch (const std::exception &e) {
		out_err = std::format("JSON parse error in {}: {}", path, e.what());
		return false;
	}
}

static void print_timing_summary(const tool_registry &registry)
{
	auto stats = registry.get_timing_stats();
	if (stats.empty()) {
		return;
	}

	struct row {
		std::string name;
		tool_registry::tool_timing_stats data;
		uint64_t total_us{0};
	};
	std::vector<row> rows;
	rows.reserve(stats.size());
	uint64_t grand_calls = 0;
	uint64_t grand_prep_us = 0;
	uint64_t grand_exec_us = 0;

	for (const auto &[name, st] : stats) {
		uint64_t tot = st.total_prepare_us + st.total_execute_us;
		rows.push_back({name, st, tot});
		grand_calls += st.calls;
		grand_prep_us += st.total_prepare_us;
		grand_exec_us += st.total_execute_us;
	}

	std::sort(rows.begin(), rows.end(), [](const row &a, const row &b) { return a.total_us > b.total_us; });

	std::cout << "\n=== Tool Execution Timing Summary ===\n";
	std::cout << std::format("{:<28} {:>7} {:>12} {:>12} {:>12} {:>12} {:>12}\n",
				 "Tool", "Calls", "Prep (us)", "Exec (us)", "Total (us)", "Avg (us)", "Max (us)");
	std::cout << std::string(101, '-') << "\n";
	for (const auto &r : rows) {
		uint64_t avg_us = r.data.calls > 0 ? (r.total_us / r.data.calls) : 0;
		std::cout << std::format("{:<28} {:>7} {:>12} {:>12} {:>12} {:>12} {:>12}\n",
					 r.name, r.data.calls, r.data.total_prepare_us, r.data.total_execute_us,
					 r.total_us, avg_us, r.data.max_total_us);
	}
	std::cout << std::string(101, '-') << "\n";
	uint64_t grand_total_us = grand_prep_us + grand_exec_us;
	uint64_t grand_avg_us = grand_calls > 0 ? (grand_total_us / grand_calls) : 0;
	std::cout << std::format("{:<28} {:>7} {:>12} {:>12} {:>12} {:>12}\n",
				 "TOTAL", grand_calls, grand_prep_us, grand_exec_us, grand_total_us, grand_avg_us);

	auto mf_stats = mapped_file::get_cache_stats();
	std::cout << std::format("mapped_file cache: entries={}, hits={}, misses={}, evictions={}, prefetches={}\n",
				 mf_stats.current_entries, mf_stats.hits, mf_stats.misses,
				 mf_stats.capacity_evictions + mf_stats.stale_evictions, mf_stats.prefetch_completed)
		  << std::flush;
}

} // namespace

int main(int argc, char **argv)
{
	CLI::App app{"Turbostar Agent CLI for E2E Testing"};
	
	std::string prompt = "How cold is it outside in San Francisco, CA?";
	std::string replay_file = "tests/data/todo_traffic.json";
	std::string dump_state_file = "";
	std::string project_dir = "";
	std::vector<std::string> active_families;
	std::vector<std::string> replay_toolcall_files;
	size_t iterations = 1;
	bool yolo_mode = false;
	bool all_tool_families = false;
	bool tooltrace = false;

	app.add_option("prompt,-p,--prompt", prompt, "The initial user prompt to send to the agent");
	app.add_option("replay,-r,--replay", replay_file, "Traffic file for replay/record modes");
	app.add_option("--dump-state", dump_state_file, "Dump the final conversation state to this JSON file before exiting");
	app.add_option("--project-dir", project_dir, "Override the project root directory for isolated sandboxing");
	app.add_option("--activate-family", active_families, "Activate specific tool families for the test agent");
	app.add_option("--replay-toolcall", replay_toolcall_files, "Directly replay/benchmark one or more toolcall.<nr> trace files");
	app.add_option("--iterations", iterations, "Number of benchmark iterations when running --replay-toolcall (default: 1)");
	app.add_flag("--yolo", yolo_mode, "Automatically approve all permission prompts and user questions (non-interactive)");
	app.add_flag("--all-tool-families", all_tool_families, "Automatically enable all registered tool families at startup");
	app.add_flag("--tooltrace", tooltrace, "Trace all tool calls to sequential log files (toolcall.0, toolcall.1...)");

	CLI11_PARSE(app, argc, argv);

	if (tooltrace) {
		tool_tracer::get_instance().set_enabled(true);
	}
	if (yolo_mode) {
		config_manager::get_instance().set_yolo_mode(true);
	}
	if (all_tool_families || !replay_toolcall_files.empty()) {
		config_manager::get_instance().set_all_tool_families_enabled(true);
	}

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

	auto registry_model = ai_model_registry::get_instance().get_default_model();
	if (registry_model) {
		default_model_id = registry_model->get_id();
		default_type = registry_model->get_api_type();
		default_name = registry_model->get_name();
		cost_tx = registry_model->get_cost_per_1m_tx();
		cost_rx = registry_model->get_cost_per_1m_rx();
		api_key = registry_model->get_api_key();
		cost_type = registry_model->get_cost_type();
	}

	tool_registry &registry = tool_registry::get_instance();
	registry.reset_timing_stats();
	mapped_file::clear_cache();

	tool_context ctx;
	event_queue q;
	auto model = std::make_shared<ai_model>(default_model_id, default_name, url, "CLI Test", cost_tx, cost_rx, api_key, default_type, 250000, cost_type);
	auto test_agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	for (const auto &fam : active_families) {
		test_agent->add_active_tool_family(fam);
	}
	ctx.active_agent = test_agent.get();
	ctx.properties = test_agent->get_properties();
	ctx.is_family_active = [&](const std::string &family) { return test_agent->is_tool_family_active(family); };

	(void)hex_highlighter_registry::get_instance();
	project_manager::get_instance().set_enforce_initialization(true);
	project_manager::get_instance().initialize();
	images::image_manager::get_instance().initialize();
	agentlib::agent_subsystems::initialize();

	std::filesystem::path workspace_root = project_dir.empty() ? std::filesystem::current_path() : std::filesystem::path(project_dir);
	ctx.fs_security.set_working_directory(workspace_root);
	ctx.fs_security.add_allowed_root(workspace_root, access_type::read);
	ctx.fs_security.add_allowed_root(workspace_root, access_type::write);
	ctx.fs_security.set_vfs(agentlib::skill_manager::get_instance().get_vfs());

	std::vector<message> conversation;

	if (!replay_toolcall_files.empty()) {
		std::vector<parsed_toolcall_file> calls;
		for (const auto &tc_file : replay_toolcall_files) {
			parsed_toolcall_file parsed;
			std::string err;
			if (!parse_toolcall_trace_file(tc_file, parsed, err)) {
				std::cerr << "Error: " << err << std::endl;
				std::_Exit(1);
			}
			calls.push_back(std::move(parsed));
		}

		size_t runs = std::max<size_t>(1, iterations);
		std::cout << std::format("[Replaying {} toolcall file(s) for {} iteration(s)]\n", calls.size(), runs);

		uint64_t cold_us = 0;
		uint64_t warm_sum_us = 0;
		uint64_t warm_min_us = UINT64_MAX;
		uint64_t warm_max_us = 0;

		for (size_t iter = 0; iter < runs; ++iter) {
			uint64_t iter_total_us = 0;
			for (const auto &tc : calls) {
				auto prep = registry.prepare_tool(tc.name, tc.args_json, ctx);
				auto exec_start = std::chrono::steady_clock::now();
				std::string res = registry.execute_prepared_tool(tc.name, tc.args_json, prep, ctx);
				auto exec_end = std::chrono::steady_clock::now();
				uint64_t exec_us = static_cast<uint64_t>(
				    std::chrono::duration_cast<std::chrono::microseconds>(exec_end - exec_start).count());
				uint64_t call_total_us = prep.prepare_duration_us + exec_us;
				iter_total_us += call_total_us;
				if (runs == 1) {
					std::cout << std::format("Executing tool: {} ({})\n", tc.name, tc.source_file);
					std::cout << std::format("[Tool Timing] {}: prep={} us, exec={} us, total={} us\n",
								 tc.name, prep.prepare_duration_us, exec_us, call_total_us);
					std::cout << "[Tool Result] " << res << std::endl;
				} else if (runs <= 10 || iter == 0 || iter + 1 == runs) {
					std::cout << std::format("[Iter {}/{}] {} ({}): prep={} us, exec={} us, total={} us\n",
								 iter + 1, runs, tc.name, tc.source_file,
								 prep.prepare_duration_us, exec_us, call_total_us);
				}
			}
			if (iter == 0) {
				cold_us = iter_total_us;
			} else {
				warm_sum_us += iter_total_us;
				warm_min_us = std::min(warm_min_us, iter_total_us);
				warm_max_us = std::max(warm_max_us, iter_total_us);
			}
		}
		if (runs > 1) {
			uint64_t warm_iters = runs - 1;
			uint64_t warm_avg_us = warm_sum_us / warm_iters;
			std::cout << std::format("[Benchmark Breakdown] Cold (Iter 1): {} us | Warm (Iter 2..{}): avg={} us, min={} us, max={} us\n",
						 cold_us, runs, warm_avg_us, warm_min_us, warm_max_us);
		}
	} else {
		// Set up the transport chain using the resolved default model details
#if defined(LLM_TRANSPORT_REPLAY)
		std::cout << "[Using Replay Transport]" << std::endl;
		auto player = std::make_shared<replay_transport>(replay_file);
		default_type = player->detect_api_type();
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

		std::cout << "Connecting to: " << url << std::endl;
		std::cout << "Prompt: " << prompt << "\n" << std::endl;

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
				std::cout << "LLM requested tool calls." << std::endl;
				// The LLM's assistant message needs to be added to the history
				conversation.push_back(response);

				for (const auto &call : *response.tool_calls) {
					std::cout << "Executing tool: " << call.function.name << std::endl;

					// Sync state to agent before execution (so tools can modify it)
					test_agent->set_conversation(conversation);

					ctx.properties = test_agent->get_properties();
					auto prep = registry.prepare_tool(call.function.name, call.function.arguments, ctx);
					auto exec_start = std::chrono::steady_clock::now();
					std::string tool_result = registry.execute_prepared_tool(call.function.name, call.function.arguments, prep, ctx);
					auto exec_end = std::chrono::steady_clock::now();
					uint64_t exec_us = static_cast<uint64_t>(
					    std::chrono::duration_cast<std::chrono::microseconds>(exec_end - exec_start).count());

					// Sync state back from agent (in case it paged out)
					conversation = test_agent->get_conversation();

					std::cout << std::format("[Tool Timing] {}: prep={} us, exec={} us, total={} us\n",
								 call.function.name, prep.prepare_duration_us, exec_us,
								 prep.prepare_duration_us + exec_us);
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
	}

	print_timing_summary(registry);

	if (!dump_state_file.empty()) {
		nlohmann::json root;
		root["stats"] = test_agent->get_stats();

		nlohmann::json timings_json = nlohmann::json::object();
		for (const auto &[tname, st] : registry.get_timing_stats()) {
			timings_json[tname] = {
				{"calls", st.calls},
				{"errors", st.errors},
				{"total_prepare_us", st.total_prepare_us},
				{"total_execute_us", st.total_execute_us},
				{"total_duration_us", st.total_prepare_us + st.total_execute_us},
				{"max_total_us", st.max_total_us},
			};
		}
		root["tool_timings"] = timings_json;

		nlohmann::json conv_array = nlohmann::json::array();
		for (const auto &msg : conversation) {
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
