// Tested source file: src/tools/fs_compile_project/fs_compile_project_entry.cpp
#include "test_watchdog.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/config_manager.h"
#include "../../src/project_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	project_manager::get_instance().initialize();

	// Save original build configuration
	std::string orig_build_system = config_manager::get_instance().get_build_system();
	std::string orig_build_dir = config_manager::get_instance().get_build_directory();

	// Set dummy build system/directory to speed up test execution
	config_manager::get_instance().set_build_system("echo CompileSuccessful");
	config_manager::get_instance().set_build_directory("");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	std::string project_root = project_manager::get_instance().get_project_root();
	ctx.fs_security.set_working_directory(project_root);
	ctx.fs_security.add_allowed_root(project_root, access_type::read);
	ctx.fs_security.add_allowed_root(project_root, access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing fs_compile_project..." << std::endl;
	{
		// 1. Success case: compile project (sync)
		{
			std::string args = "{\"clean\": false, \"async\": false}";
			std::string res = registry.execute_tool("fs_compile_project", args, ctx);
			std::cout << "Compile project sync result: " << res << std::endl;
			assert(!res.empty());
		}

		// 1b. Success case: compile project with clean=true (sync)
		{
			std::string args = "{\"clean\": true, \"async\": false}";
			std::string res = registry.execute_tool("fs_compile_project", args, ctx);
			std::cout << "Compile project sync result (clean=true): " << res << std::endl;
			assert(!res.empty());
		}

		// 2. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			std::string args = "{\"clean\": false, \"async\": false, \"unexpected_arg\": 123}";
			auto prep = registry.prepare_tool("fs_compile_project", args, ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 3. Success case: compile project (async)
		{
			std::string args = "{\"clean\": false, \"async\": true}";
			std::string res = registry.execute_tool("fs_compile_project", args, ctx);
			std::cout << "Compile project async result: " << res << std::endl;
			assert(res.find("background") != std::string::npos);

			// Now wait for the detached thread to run and update the agent
			bool found_msg = false;
			for (int i = 0; i < 100; ++i) {
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				auto conv = agent->get_conversation();
				for (const auto &msg : conv) {
					if (msg.role == "system" && (msg.content.find("successfully") != std::string::npos ||
								     msg.content.find("with errors") != std::string::npos)) {
						found_msg = true;
						std::cout << "Found expected async message: " << msg.content << std::endl;
						break;
					}
				}
				if (found_msg)
					break;
			}
			assert(found_msg);
		}

		// 4. Failure case with dirty file health state: must include attribution note
		{
			config_manager::get_instance().set_build_system("echo 'main.cpp:10: error: expected declaration' && false");
			ctx.file_health_tracker["src/main.cpp"].state = lsp_health_state::dirty;
			ctx.file_health_tracker["src/main.cpp"].originating_edit_id = "#2";
			std::string args = "{\"clean\": false, \"async\": false}";
			std::string res = registry.execute_tool("fs_compile_project", args, ctx);
			std::cout << "Compile project failure result: " << res << std::endl;
			assert(res.find("Diagnostic Note: File 'src/main.cpp' was clean and first developed errors after Edit #2.") != std::string::npos);
		}

		// 5. Successful compilation clears dirty file health states
		{
			config_manager::get_instance().set_build_system("echo compilesuccessful");
			std::string args = "{\"clean\": false, \"async\": false}";
			std::string res = registry.execute_tool("fs_compile_project", args, ctx);
			assert(ctx.file_health_tracker["src/main.cpp"].state == lsp_health_state::clean);
			assert(ctx.file_health_tracker["src/main.cpp"].originating_edit_id.empty());
		}

		std::cout << "fs_compile_project tool verified successfully!" << std::endl;
	}

	// Restore original build configuration
	config_manager::get_instance().set_build_system(orig_build_system);
	config_manager::get_instance().set_build_directory(orig_build_dir);

	return 0;
}
