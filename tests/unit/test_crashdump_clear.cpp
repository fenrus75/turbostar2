// Tested source file: src/tools/crashdump_clear/crashdump_clear_entry.cpp, src/tools/crashdump_clear/crashdump_clear_security.cpp
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "agentlib/tool_context.h"
#include "agentlib/tool_registry.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "test_watchdog.h"
#include "tools/crashdump_clear/crashdump_clear.h"

namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);
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
	{
		std::ofstream ofs(crash_path / "maps.txt");
		ofs << "555555554000-555555555000 r-xp 00000000 08:01 123456 /usr/bin/turbostar\n";
	}
	{
		std::ofstream ofs(crash_path / "stack.bin", std::ios::binary);
		uint64_t ip = 0x555555554020;
		ofs.write(reinterpret_cast<const char *>(&ip), sizeof(ip));
	}

	// 2. Populate the manager
	crashdump_manager::get_instance().refresh("dummy_hash");
	const auto &dumps = crashdump_manager::get_instance().get_crashdumps();
	assert(!dumps.empty());

	// Assert on extracted executable name
	assert(dumps[0].executable == "turbostar");

	// Assert on extracted timestamp format (YYYY-MM-DD HH:MM:SS)
	assert(dumps[0].timestamp != "Recent");
	assert(dumps[0].timestamp.length() == 19);
	assert(dumps[0].timestamp[4] == '-');
	assert(dumps[0].timestamp[7] == '-');

	// Check report.md content
	fs::path report_path = crash_path / "report.md";
	assert(fs::exists(report_path));

	// 3. Execute the tool
	tools::crashdump_clear_tool tool;
	agentlib::tool_context ctx;

	std::string result = tool.execute(ctx);
	assert(fs_utils::unwrap_prompt_untrusted_data_tag(result) == "Successfully cleared all crash dumps.");

	// 4. Verify results
	assert(crashdump_manager::get_instance().get_crashdumps().empty());
	assert(!fs::exists(crash_path));

	// 5. Test validator accepting crash_id and id alias
	agentlib::tool_registry &registry = agentlib::tool_registry::get_instance();
	auto prep = registry.prepare_tool("crashdump_clear", "{\"crash_id\": \"test123\"}", ctx);
	assert(prep.tool != nullptr);
	assert(prep.error_message.empty());

	auto prep_alias = registry.prepare_tool("crashdump_clear", "{\"id\": \"test123\"}", ctx);
	assert(prep_alias.tool != nullptr);
	assert(prep_alias.error_message.empty());

	// 6. Test clearing a specific crash dump by ID
	fs::path crash1 = fs::path(dump_dir) / "crash_111";
	fs::path crash2 = fs::path(dump_dir) / "crash_222";
	fs::create_directories(crash1);
	fs::create_directories(crash2);
	{
		std::ofstream ofs(crash1 / "info.txt");
		ofs << "Signal: 11\n";
	}
	{
		std::ofstream ofs(crash2 / "info.txt");
		ofs << "Signal: 6\n";
	}
	crashdump_manager::get_instance().refresh();
	assert(crashdump_manager::get_instance().get_crashdumps().size() == 2);

	tools::crashdump_clear_tool specific_tool("111");
	std::string spec_res = specific_tool.execute(ctx);
	assert(fs_utils::unwrap_prompt_untrusted_data_tag(spec_res) == "Successfully cleared crash dump 111.");
	assert(!fs::exists(crash1));
	assert(fs::exists(crash2));

	// Clean up remaining
	tools::crashdump_clear_tool clear_all_tool;
	clear_all_tool.execute(ctx);
	assert(!fs::exists(crash2));

	std::cout << "crashdump_clear tool test passed!\n";
	return 0;
}
