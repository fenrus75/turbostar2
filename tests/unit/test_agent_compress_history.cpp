#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/config_manager.h"
#include "../../src/event_logger.h"
#include "../../src/event_queue.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"
#include "tools/agent_compress_history/agent_compress_history.h"

using namespace agentlib;

int main()
{
	event_logger::get_instance().enable_stdout_logging(true);
	project_manager::get_instance().initialize();

	// Clear history directory to ensure clean test state
	std::string history_dir = fs_utils::get_project_history_dir("TestAgent");
	if (std::filesystem::exists(history_dir)) {
		std::filesystem::remove_all(history_dir);
	}

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost:1", "Test", 0.0, 0.0);
	ai_model_registry::get_instance().register_model(model);
	config_manager::get_instance().set_default_model_id("test-model");
	auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
	ctx.active_agent = agent.get();

	// Inject some conversational context to compress
	agent->inject_context("user", "Hello world");
	agent->inject_context("assistant", std::string(1100, 'x'));
	agent->inject_context("user", "Let's do some work.");
	agent->inject_context("assistant", "Sure, what do you want to do?");
	agent->inject_context("user", "Write a python script.");
	agent->inject_context("assistant", "Okay, here is a python script.");
	agent->inject_context("user", "Now run it.");
	agent->inject_context("assistant", "It ran successfully.");

	std::cout << "Testing agent_compress_history..." << std::endl;
	std::cout << "Conversation size: " << agent->get_conversation().size() << std::endl;
	{
		// 1. Successful execution
		std::string compress_res = registry.execute_tool(
		    "agent_compress_history",
		    "{\"title\": \"Milestone 1\", \"summary\": \"Completed first step.\", \"include_all_prior\": true}", ctx);
		std::cout << "Result: " << compress_res << std::endl;
		assert(compress_res.find("successfully") != std::string::npos);

		// 2. Rejection of empty title (validation fails at runtime)
		{
			tools::agent_compress_history_tool tool1({"", "summary", {}, "", false});
			std::string err;
			assert(tool1.validate_runtime(ctx, err) == false);
			assert(err.find("title") != std::string::npos);
		}

		// 3. Rejection of empty summary
		{
			tools::agent_compress_history_tool tool2({"title", "", {}, "", false});
			std::string err;
			assert(tool2.validate_runtime(ctx, err) == false);
			assert(err.find("summary") != std::string::npos);
		}

		// 4. Rejection of overly long title (> 200 characters)
		{
			std::string long_title(201, 'a');
			auto prep = registry.prepare_tool("agent_compress_history",
							  "{\"title\": \"" + long_title + "\", \"summary\": \"summary\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Title exceeds") != std::string::npos);
		}

		// 5. Rejection of overly long summary (> 2000 characters)
		{
			std::string long_summary(2001, 'b');
			auto prep = registry.prepare_tool("agent_compress_history",
							  "{\"title\": \"title\", \"summary\": \"" + long_summary + "\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Summary exceeds") != std::string::npos);
		}

		// 6. Rejection of unsafe control characters in title
		{
			auto prep = registry.prepare_tool("agent_compress_history",
							  "{\"title\": \"unsafe\\u001btitle\", \"summary\": \"summary\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("unsafe control characters") != std::string::npos);
		}

		// 7. Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		auto prep =
		    registry.prepare_tool("agent_compress_history", "{\"title\": \"Valid title\", \"summary\": \"Valid summary\"}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::agent_compress_history_tool direct_tool({"Valid title", "Valid summary", {}, "", false});
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);

		// Wait for background summary worker thread to process and fail (connecting to localhost)
		std::cout << "Waiting for background summarization to fail..." << std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(200));

		// Check the metadata files to ensure reactivation_hint does not store the error message
		std::string history_dir = fs_utils::get_project_history_dir(agent->get_name());
		bool found_metadata = false;
		for (const auto &entry : std::filesystem::directory_iterator(history_dir)) {
			std::string filename = entry.path().filename().string();
			if (filename.ends_with("_metadata.json")) {
				found_metadata = true;
				std::ifstream f(entry.path());
				nlohmann::json root;
				f >> root;
				std::string hint = root.value("reactivation_hint", "");
				std::cout << "Metadata file: " << filename << ", reactivation_hint: '" << hint << "'" << std::endl;
				// The reactivation hint must not contain the connection error message
				assert(hint.find("Error connecting to LLM server") == std::string::npos);
			}
		}
		assert(found_metadata);

		// 8. Test 64k tokens (256,000 characters) hard limit in evaluate_auto_episode
		{
			auto test_model =
			    std::make_shared<ai_model>("test-model-2", "Test Model 2", "http://localhost:1", "Test", 0.0, 0.0);
			auto test_agent = ai_agent::create(2, "TestAgent2", test_model, &q, nullptr);

			// We need at least two turns so parse_turns doesn't early exit or we just test the character limit check
			test_agent->inject_context("user", "Start");
			test_agent->inject_context("assistant", "Sure");
			test_agent->inject_context("user", std::string(200000, 'a')); // > 192,000 but <= 256,000

			auto convo = test_agent->get_conversation();
			test_agent->evaluate_auto_episode(convo);

			// Under the old 48k limit (192,000 chars), this would have split, appending a system message "Episode Archived..."
			// We assert that under the new 64k limit, it has NOT split yet.
			auto updated_convo = test_agent->get_conversation();
			bool has_archive_msg = false;
			for (const auto &msg : updated_convo) {
				if (msg.role == "system" && msg.content.find("Episode Archived") != std::string::npos) {
					has_archive_msg = true;
				}
			}
			assert(!has_archive_msg);

			// Now inject more characters to cross 256,000
			test_agent->inject_context("assistant", "Understood");
			test_agent->inject_context("user", std::string(60000, 'b')); // Total recent chars is now > 260,000

			convo = test_agent->get_conversation();
			test_agent->evaluate_auto_episode(convo);

			// Under the new 64k limit, this must now trigger a split and append the archived message
			updated_convo = test_agent->get_conversation();
			has_archive_msg = false;
			for (const auto &msg : updated_convo) {
				if (msg.role == "system" && msg.content.find("Episode Archived") != std::string::npos) {
					has_archive_msg = true;
				}
			}
			assert(has_archive_msg);
		}

		// 9. Test fallback summary for large episode (> 250,000 characters)
		{
			std::string history_dir = fs_utils::get_project_history_dir("TestAgent3");
			if (std::filesystem::exists(history_dir)) {
				std::filesystem::remove_all(history_dir);
			}

			auto test_model =
			    std::make_shared<ai_model>("test-model-3", "Test Model 3", "http://localhost:1", "Test", 0.0, 0.0);
			test_model->set_max_context_tokens(64000);
			ai_model_registry::get_instance().register_model(test_model);
			config_manager::get_instance().set_default_model_id("test-model-3");
			auto test_agent = ai_agent::create(3, "TestAgent3", test_model, &q, nullptr);
			ctx.active_agent = test_agent.get();

			// Inject 260,000 characters
			test_agent->inject_context("user", "Start big");
			test_agent->inject_context("assistant", std::string(260000, 'x'));
			test_agent->inject_context("user", "Follow up");
			test_agent->inject_context("assistant", "Sure");

			// Execute history compression
			std::string compress_res = registry.execute_tool(
			    "agent_compress_history",
			    "{\"title\": \"Huge Milestone\", \"summary\": \"Heavy compilation errors.\", \"include_all_prior\": true}",
			    ctx);
			assert(compress_res.find("successfully") != std::string::npos);

			// Wait for background summary worker to run (it should finish instantly using fallback)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

			history_dir = fs_utils::get_project_history_dir(test_agent->get_name());
			bool found_meta = false;
			for (const auto &entry : std::filesystem::directory_iterator(history_dir)) {
				std::string filename = entry.path().filename().string();
				if (filename.find("episode_") != std::string::npos && filename.ends_with("_metadata.json")) {
					found_meta = true;
					std::ifstream f(entry.path());
					nlohmann::json root;
					f >> root;
					std::string hint = root.value("reactivation_hint", "");
					std::cout << "Large episode metadata hint: '" << hint << "'" << std::endl;
					assert(hint.find("Large episode") != std::string::npos);
					assert(hint.find("Huge Milestone") != std::string::npos);
				}
			}
			assert(found_meta);
		}

		// 10. Test page_in_history_auto backward-paging with 50% context window limit
		{
			auto test_model =
			    std::make_shared<ai_model>("test-model-4", "Test Model 4", "http://localhost:1", "Test", 0.0, 0.0);
			test_model->set_max_context_tokens(1000); // 50% is 500 tokens
			ai_model_registry::get_instance().register_model(test_model);
			config_manager::get_instance().set_default_model_id("test-model-4");
			auto test_agent = ai_agent::create(4, "TestAgent4", test_model, &q, nullptr);

			// Clean/ensure empty history first
			std::string hist_dir = fs_utils::get_project_history_dir("TestAgent4");
			if (std::filesystem::exists(hist_dir)) {
				std::filesystem::remove_all(hist_dir);
			}

			// Add some base messages (e.g. system message / user messages)
			test_agent->inject_context("user", "Base message"); // ~10 tokens

			// Episode 1 (oldest): Title A, ~100 tokens
			test_agent->inject_context("assistant", std::string(400, 'x')); // 100 tokens
			test_agent->page_out_context(1, 2, "Milestone A", "Summary A", {"tagA"});

			// Episode 2: Title B, ~200 tokens
			test_agent->inject_context("assistant", std::string(800, 'y')); // 200 tokens
			test_agent->page_out_context(2, 3, "Milestone B", "Summary B", {"tagB"});

			// Episode 3 (newest): Title C, ~400 tokens
			test_agent->inject_context("assistant", std::string(1600, 'z')); // 400 tokens
			test_agent->page_out_context(3, 4, "Milestone C", "Summary C", {"tagC"});

			// Now, max_context_tokens is 1000, 50% limit is 500 tokens.
			// Base message is ~10 tokens.
			// The three paged-out episodes are:
			// - episode_3 (newest, ~400 tokens)
			// - episode_2 (middle, ~200 tokens)
			// - episode_1 (oldest, ~100 tokens)
			//
			// If we call page_in_history_auto(1), it should:
			// 1. Consider episode_3: fits (10 + 400 = 410 <= 500). Paged in.
			// 2. Consider episode_2: does not fit (410 + 200 = 610 > 500). Stop.
			// 3. episode_1 should not be considered/paged in because we stopped.
			//
			// So only episode_3 should be paged in!
			std::vector<std::string> paged_in = test_agent->page_in_history_auto(1);
			std::cout << "Paged-in count: " << paged_in.size() << std::endl;
			for (const auto &id : paged_in) {
				std::cout << "  Paged in ID: " << id << std::endl;
			}

			assert(paged_in.size() == 1);
			assert(paged_in[0] == "episode_3");
		}

		// 9. Test stripping of <think>...</think> tags on compaction
		{
			auto test_model =
			    std::make_shared<ai_model>("test-model-3", "Test Model 3", "http://localhost:1", "Test", 0.0, 0.0);
			auto test_agent = ai_agent::create(3, "TestAgent3", test_model, &q, nullptr);

			// Inject message with <think> tag
			test_agent->inject_context("user", "Hello");
			test_agent->inject_context("assistant", "<think>Some inner thought here.</think>Here is the actual answer.");

			// Page out to level 1
			test_agent->page_out_context(1, 2, "Milestone Think", "Summary Think", {"tagThink"});

			// Page in
			test_agent->page_in_context("episode_1", 1);

			// Check conversation content
			bool found_assistant = false;
			for (const auto& msg : test_agent->get_conversation()) {
				if (msg.role == "assistant") {
					found_assistant = true;
					std::cout << "Compacted content: '" << msg.content << "'" << std::endl;
					assert(msg.content.find("<think>") == std::string::npos);
					assert(msg.content.find("inner thought") == std::string::npos);
					assert(msg.content.find("actual answer") != std::string::npos);
				}
			}
			assert(found_assistant);
			std::cout << "Test 9 passed: <think> tags stripped successfully.\n";

			// Cleanup
			std::string h_dir = fs_utils::get_project_history_dir("TestAgent3");
			if (std::filesystem::exists(h_dir)) {
				std::filesystem::remove_all(h_dir);
			}
		}

		std::cout << "agent_compress_history tool verified successfully!" << std::endl;
	}

	return 0;
}
