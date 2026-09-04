// Tested source file: src/crashdump_manager.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include "agentlib/ai_agent.h"
#include "agentlib/tool_registry.h"
#include "crashdump_manager.h"
#include "fs_utils.h"
#include "project_manager.h"

using namespace agentlib;
namespace fs = std::filesystem;

int main()
{
	test_watchdog::setup_watchdog(30);
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
	{
		std::ofstream ofs(crash_path / "report.md");
		ofs << "Mock crash dump backtrace detail\n";
	}
	{
		std::ofstream ofs(crash_path / "core");
		ofs << "mock core dump bytes\n";
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

	std::cout << "Testing crashdump_get_info..." << std::endl;
	{
		// 1. Success case: retrieve detailed info of crash_test123 and verify coredump instruction is present
		{
			std::string res = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"test123\"}", ctx);
			std::cout << "Crash info result: " << res << std::endl;
			assert(res.find("Mock crash dump backtrace detail") != std::string::npos);
			assert(res.find("agent_debug_coredump") != std::string::npos);
			assert(res.find("test123") != std::string::npos);
		}

		// 2. Execution failure case: nonexistent crash_id
		{
			std::string res = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"nonexistent\"}", ctx);
			std::cout << "Nonexistent ID result: " << res << std::endl;
			assert(res.find("Error: No crashdump found") != std::string::npos);
		}

		// 3. Stage 1 validation failure: reject empty crash_id (based on review recommendations)
		{
			auto prep = registry.prepare_tool("crashdump_get_info", "{\"crash_id\": \"\"}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		// 4. Stage 1 validation failure: reject unexpected properties (based on review recommendations)
		{
			auto prep = registry.prepare_tool("crashdump_get_info", "{\"crash_id\": \"test123\", \"extra_arg\": 1}", ctx);
			assert(prep.tool == nullptr); // This will fail initially as expected
			assert(!prep.error_message.empty());
		}

		std::cout << "crashdump_get_info tool verified successfully!" << std::endl;
	}

	// 5. Verify report generation fallback when no core exists (unwinder path)
	{
		fs::path crash_fallback = fs::path(dump_dir) / "crash_fallback456";
		fs::create_directories(crash_fallback);
		{
			std::ofstream ofs(crash_fallback / "info.txt");
			ofs << "Signal: 6\nExecutable: /bin/true\n";
		}
		{
			std::ofstream ofs(crash_fallback / "maps.txt");
			ofs << "00000000-ffffffff r-xp 00000000 00:00 0 /bin/true\n";
		}
		{
			std::ofstream ofs(crash_fallback / "stack.bin", std::ios::binary);
			uint64_t ip = 0x12345;
			ofs.write(reinterpret_cast<const char*>(&ip), sizeof(ip));
		}
		crashdump_manager::get_instance().refresh("dummy_hash");
		std::string res = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"fallback456\"}", ctx);
		assert(res.find("| Frame | Address | Function | Location |") != std::string::npos);
		std::cout << "Fallback report generation verified!" << std::endl;
	}

	// 6. Verify GDB enrichment when core file and executable are available
	{
		std::string proj_root = project_manager::get_instance().get_project_root();
		fs::path crash_exe = fs::path(proj_root) / "build" / "crash";
		fs::path found_core;
		if (fs::exists(crash_exe)) {
			for (const auto &entry : fs::recursive_directory_iterator(dump_dir)) {
				if (entry.is_regular_file() && entry.path().filename().string().starts_with("core") && entry.file_size() > 1024) {
					found_core = entry.path();
					break;
				}
			}
		}
		if (!found_core.empty()) {
			fs::path crash_enriched = fs::path(dump_dir) / "crash_enriched789";
			fs::create_directories(crash_enriched);
			{
				std::ofstream ofs(crash_enriched / "info.txt");
				ofs << "Signal: 6\nExecutable: " << crash_exe.string() << "\n";
			}
			{
				std::ofstream ofs(crash_enriched / "maps.txt");
				ofs << "00000000-ffffffff r-xp 00000000 00:00 0 " << crash_exe.string() << "\n";
			}
			{
				std::ofstream ofs(crash_enriched / "stack.bin", std::ios::binary);
				uint64_t ip = 0x12345;
				ofs.write(reinterpret_cast<const char*>(&ip), sizeof(ip));
			}
			fs::copy_file(found_core, crash_enriched / "core", fs::copy_options::overwrite_existing);

			crashdump_manager::get_instance().refresh("dummy_hash");
			std::string res = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"enriched789\"}", ctx);
			assert(res.find("| Frame | Address | Function | Location | Note |") != std::string::npos);
			assert(res.find("crash handling") != std::string::npos);
			std::cout << "GDB enriched report generation verified!" << std::endl;
		}
	}

	// Clean up mock files
	crashdump_manager::get_instance().clear_all();
	return 0;
}
