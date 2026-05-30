#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/tool_registry.h"
#include "../../src/crashdump_manager.h"
#include "../../src/fs_utils.h"
#include "../../src/project_manager.h"

using namespace agentlib;
namespace fs = std::filesystem;

int main()
{
	project_manager::get_instance().initialize();

	// Clear previous state and setup mock directories
	crashdump_manager::get_instance().clear_all();
	assert(crashdump_manager::get_instance().get_crashdumps().empty());

	std::string dump_dir = fs_utils::get_project_dump_dir();
	fs::path crash_path = fs::path(dump_dir) / "crash_test123";
	fs::create_directories(crash_path);

	{
		std::ofstream ofs(crash_path / "info.txt");
		ofs << "Signal: 11\n";
	}
	{
		std::ofstream ofs(crash_path / "maps.txt");
		ofs << "555555554000-555555555000 r-xp 00000000 08:01 123456 /usr/bin/turbostar\n";
	}

	// Populate the manager
	crashdump_manager::get_instance().refresh("dummy_hash");

	tool_registry &registry = tool_registry::get_instance();
	tool_context ctx;

	ctx.fs_security.set_working_directory(project_manager::get_instance().get_project_root());
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::read);
	ctx.fs_security.add_allowed_root(project_manager::get_instance().get_project_root(), access_type::write);

	auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = ai_agent::create(1, "TestAgent", model, nullptr, nullptr);
	ctx.active_agent = agent.get();

	std::cout << "Testing crashdump_list..." << std::endl;
	{
		// 1. Success case: list crashdumps
		{
			std::string res = registry.execute_tool("crashdump_list", "{}", ctx);
			std::cout << "Crash list result: " << res << std::endl;
			assert(res.find("| Crash ID |") != std::string::npos);
			assert(res.find("test123") != std::string::npos);
		}

		// 2. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("crashdump_list", "{\"unexpected_arg\": 1}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "crashdump_list tool verified successfully!" << std::endl;
	}

	// Clean up mock files
	crashdump_manager::get_instance().clear_all();
	return 0;
}
