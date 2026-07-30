#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/subagent_manager.h"
#include "../../src/project_manager.h"
#include "a2a/a2a_server_manager.h"
#include "tools/invoke_subagent/invoke_subagent.h"
#include "agentlib/data/conversation.h"
#include "agentlib/data/episode.h"
#include "agentlib/data/transaction.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();
	subagent_manager::get_instance().initialize();

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;
	static event_queue q;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing invoke_subagent..." << std::endl;
	{
		// 1. Success case (async)
		std::string result = registry.execute_tool("invoke_subagent",
			"{\"name\": \"sub1\", \"task\": \"Perform help\"}", ctx);
		std::cout << "Result: " << result << std::endl;
		assert(result.find("sub1") != std::string::npos);
		assert(result.find("successfully") != std::string::npos);

		// 2. Reject empty name
		{
			auto prep = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("name cannot be empty") != std::string::npos);
		}

		// 3. Reject empty task and profile
		{
			auto prep = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"sub2\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("either a 'subagent_name', 'profile', or 'task'") != std::string::npos);
		}

		// 4. Reject overly long name (> 64 characters)
		{
			std::string long_name(65, 'x');
			auto prep = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"" + long_name + "\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("name exceeds") != std::string::npos);
		}

		// 5. Reject overly long profile (> 10000 characters)
		{
			std::string long_profile(10001, 'y');
			auto prep = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"sub3\", \"profile\": \"" + long_profile + "\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("Profile exceeds") != std::string::npos);
		}

		// 6. Reject control characters in name
		{
			auto prep = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"sub\\u001bname\", \"task\": \"task\"}", ctx);
			assert(prep.tool == nullptr);
			assert(prep.error_message.find("unsafe control characters") != std::string::npos);
		}

		// 6b. Verify subagent profile creation and list subagents
		{
			// Verify list_subagents tool returns research and self
			std::string list_res = registry.execute_tool("list_subagents", "{}", ctx);
			assert(list_res.find("research") != std::string::npos);
			assert(list_res.find("self") != std::string::npos);

			// Try to prepare tool with a non-existent subagent name (should fail)
			auto prep_fail = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"sub_bad\", \"subagent_name\": \"non_existent_profile\"}", ctx);
			assert(prep_fail.tool == nullptr);
			assert(prep_fail.error_message.find("not found") != std::string::npos);

			// Try to prepare and execute tool with a valid subagent name (should succeed)
			auto prep_ok = registry.prepare_tool("invoke_subagent",
				"{\"name\": \"sub_research\", \"subagent_name\": \"research\", \"task\": \"Perform scan\"}", ctx);
			assert(prep_ok.tool != nullptr);
			assert(prep_ok.error_message.empty());

			// 6c. Test A2A remote server routing and local_only restriction
			{
				a2a::a2a_server_config s_remote;
				s_remote.name = "devpc";
				s_remote.url = "http://127.0.0.1:7860";
				s_remote.tier = a2a::a2a_server_tier::ephemeral_runtime;
				a2a::a2a_server_manager::get_instance().add_server(s_remote);

				// Reject remote subagent when local_only is true
				auto prep_remote_localonly = registry.prepare_tool("invoke_subagent",
					"{\"name\": \"rem1\", \"subagent_name\": \"devpc:research\", \"task\": \"Remote scan\", \"local_only\": true}", ctx);
				assert(prep_remote_localonly.tool == nullptr);
				assert(prep_remote_localonly.error_message.find("local_only is true") != std::string::npos);

				// Reject unregistered A2A server name
				auto prep_unregistered = registry.prepare_tool("invoke_subagent",
					"{\"name\": \"rem2\", \"subagent_name\": \"unknown_server:research\", \"task\": \"Remote scan\", \"local_only\": false}", ctx);
				assert(prep_unregistered.tool == nullptr);
				assert(prep_unregistered.error_message.find("not found in server registry") != std::string::npos);

				// Succeed for registered server with local_only false
				auto prep_remote_ok = registry.prepare_tool("invoke_subagent",
					"{\"name\": \"rem3\", \"subagent_name\": \"devpc:research\", \"task\": \"Remote scan\", \"local_only\": false}", ctx);
				assert(prep_remote_ok.tool != nullptr);
				assert(prep_remote_ok.error_message.empty());
			}
		}

		// 7. Rejection if agent is read-only
		auto original_ro = agent->is_read_only();
		agent->set_read_only(true);
		auto prep = registry.prepare_tool("invoke_subagent",
			"{\"name\": \"sub_ro\", \"task\": \"task\"}", ctx);
		assert(prep.tool == nullptr);
		assert(prep.error_message.find("read-only") != std::string::npos);

		// Directly test validate_runtime on the tool under read-only state
		{
			tools::invoke_subagent_args sub_args;
			sub_args.name = "sub_ro";
			sub_args.profile = "profile";
			sub_args.task = "task";
			sub_args.wait = false;
			tools::invoke_subagent_tool direct_tool(sub_args);
			std::string direct_err;
			assert(direct_tool.validate_runtime(ctx, direct_err) == false);
			assert(direct_err.find("read-only") != std::string::npos);
		}

		agent->set_read_only(original_ro);

		// Wait for sub1 to complete background task so we don't have race conditions on exit
		auto subagents = agent->get_subagents();
		assert(!subagents.empty());
		subagents[0]->close();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		// 8. Test merging of consecutive system context injections
		{
			std::cout << "Testing system context merging..." << std::endl;
			auto test_agent = ai_agent::create(2, "TestMergeAgent", model, nullptr, nullptr);
			test_agent->inject_context("system", "First system message");
			test_agent->inject_context("system", "Second system message");
			test_agent->inject_context("user", "User message");
			test_agent->inject_context("system", "Third system message (should not merge with user in between)");

			auto ep = test_agent->get_conversation_data()->get_current_episode();
			assert(ep != nullptr);
			const auto &transactions = ep->get_transactions();
			assert(transactions.size() == 3);

			// First transaction should have 1 turn containing the merged text
			assert(transactions[0]->get_turns().size() == 1);
			assert(transactions[0]->get_turns()[0]->get_content().starts_with("First system message\n\nSecond system message"));

			// Second transaction should be the user message
			assert(transactions[1]->get_turns().size() == 1);
			assert(transactions[1]->get_turns()[0]->get_content() == "User message");

			// Third transaction should be the third system message
			assert(transactions[2]->get_turns().size() == 1);
			assert(transactions[2]->get_turns()[0]->get_content().starts_with("Third system message (should not merge with user in between)"));
		}

		std::cout << "agent_create tool verified successfully!" << std::endl;
	}

	return 0;
}
