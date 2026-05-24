#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "../../src/tools/crashdump_clear/crashdump_clear.h"
#include "../../src/crashdump_manager.h"
#include "../../src/agentlib/tool_context.h"
#include "../../src/fs_utils.h"

namespace fs = std::filesystem;

int main()
{
	// 0. Ensure we start clean
	crashdump_manager::get_instance().clear_all();
	assert(crashdump_manager::get_instance().get_crashdumps().empty());

	// 1. Setup: Ensure a crash dump directory exists and has some content
	std::string dump_dir = fs_utils::get_project_dump_dir();
	fs::path crash_path = fs::path(dump_dir) / "crash_test123";
	fs::create_directories(crash_path);
	
	{
		std::ofstream ofs(crash_path / "info.txt");
		ofs << "Signal: 11\n";
	}
	
	// 2. Populate the manager
	crashdump_manager::get_instance().refresh("dummy_hash");
	assert(!crashdump_manager::get_instance().get_crashdumps().empty());
	
	// 3. Execute the tool
	tools::crashdump_clear_tool tool;
	agentlib::tool_context ctx;
	
	std::string result = tool.execute(ctx);
	assert(result == "Successfully cleared all crash dumps.");
	
	// 4. Verify results
	assert(crashdump_manager::get_instance().get_crashdumps().empty());
	assert(!fs::exists(crash_path));

	std::cout << "crashdump_clear tool test passed!\n";
	return 0;
}
