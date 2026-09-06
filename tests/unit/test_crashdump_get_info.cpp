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
			assert(res.find("Optional: Interactive Coredump Debugging") != std::string::npos);
			assert(res.find("If (and only if) the report above is insufficient") != std::string::npos);
			assert(res.find("agent_debug_coredump") != std::string::npos);
			assert(res.find("test123") != std::string::npos);
		}

		// 2. Execution failure case: nonexistent crash_id
		{
			std::string res = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"nonexistent\"}", ctx);
			std::cout << "Nonexistent ID result: " << res << std::endl;
			assert(res.find("Error: No crashdump found") != std::string::npos);
		}

		// 3. Optional crash_id: defaults to most recent crash chronologically
		{
			std::string res = registry.execute_tool("crashdump_get_info", "{}", ctx);
			assert(res.find("Mock crash dump backtrace detail") != std::string::npos);
			assert(res.find("test123") != std::string::npos);

			std::string res_empty = registry.execute_tool("crashdump_get_info", "{\"crash_id\": \"\"}", ctx);
			assert(res_empty.find("Mock crash dump backtrace detail") != std::string::npos);
			assert(res_empty.find("test123") != std::string::npos);

			// Add an older dump and a newer dump with distinct mtimes to verify chronological selection
			fs::path c_older = fs::path(dump_dir) / "crash_older1";
			fs::create_directories(c_older);
			{
				std::ofstream ofs(c_older / "info.txt");
				ofs << "Signal: 11\nExecutable: /bin/test_app\n";
			}
			fs::path c_newer = fs::path(dump_dir) / "crash_newer2";
			fs::create_directories(c_newer);
			{
				std::ofstream ofs(c_newer / "info.txt");
				ofs << "Signal: 11\nExecutable: /bin/test_app\n";
			}
			auto now = fs::file_time_type::clock::now();
			fs::last_write_time(c_older, now - std::chrono::hours(2));
			fs::last_write_time(c_newer, now + std::chrono::hours(2));

			crashdump_manager::get_instance().refresh("dummy_hash");

			// Should resolve to 'newer2' because its timestamp/mtime is newest
			std::string res_chrono = registry.execute_tool("crashdump_get_info", "{}", ctx);
			assert(res_chrono.find("newer2") != std::string::npos);
		}

		// 4. Stage 1 validation failure: reject unexpected properties (schema validation)
		{
			auto prep = registry.prepare_tool("crashdump_get_info", "{\"crash_id\": \"test123\", \"extra_arg\": 1}", ctx);
			assert(prep.tool == nullptr); // unexpected properties must be rejected by schema validation (tool == nullptr, error set)
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
		} else {
			std::cout << "INFO: GDB enrichment test skipped (no live core file/executable present)" << std::endl;
		}
	}

	// 7. Verify crashdump_manager::format_crash_notification format for 1 crash and multiple crashes
	{
		crashdump_manager::get_instance().clear_all();

		// Case A: 0 crashes
		assert(crashdump_manager::format_crash_notification(0).empty());

		// Case B: Exactly 1 crash
		fs::path c1 = fs::path(dump_dir) / "crash_test999";
		fs::create_directories(c1);
		{
			std::ofstream ofs(c1 / "info.txt");
			ofs << "Signal: 11 (SIGSEGV)\nExecutable: /bin/test_app\n";
		}
		crashdump_manager::get_instance().refresh("dummy_hash");

		std::string notif1 = crashdump_manager::format_crash_notification(1);
		std::cout << "1 crash notification:\n" << notif1 << "\n";
		assert(notif1.find("CRASH DETECTED: Application crashed (Crash ID: test999)") != std::string::npos);
		assert(notif1.find("Please use 'crashdump_get_info' with crash_id 'test999'") != std::string::npos);

		// Case C: Multiple crashes (2 crashes)
		fs::path c2 = fs::path(dump_dir) / "crash_test1000";
		fs::create_directories(c2);
		{
			std::ofstream ofs(c2 / "info.txt");
			ofs << "Signal: 6 (SIGABRT)\nExecutable: /bin/test_app2\n";
		}
		crashdump_manager::get_instance().refresh("dummy_hash");

		std::string notif2 = crashdump_manager::format_crash_notification(2);
		std::cout << "2 crashes notification:\n" << notif2 << "\n";
		assert(notif2.find("2 crash(es) occurred during execution") != std::string::npos);
		assert(notif2.find("test999") != std::string::npos);
		assert(notif2.find("test1000") != std::string::npos);
		assert(notif2.find("Please use 'crashdump_list' and 'crashdump_get_info' to investigate.") != std::string::npos);
	}

	// 8. Verify clean_function_signature and classify_location
	{
		// A. clean_function_signature removes <optimized out> arguments and preserves valid args
		std::string f1 = crashdump_manager::clean_function_signature(
			"__pthread_kill_implementation (threadid=<optimized out>, signo=signo@entry=6, no_tid=no_tid@entry=0)");
		std::cout << "Cleaned f1: " << f1 << std::endl;
		assert(f1 == "__pthread_kill_implementation (signo=signo@entry=6, no_tid=no_tid@entry=0)");

		std::string f2 = crashdump_manager::clean_function_signature(
			"__GI___wait4 (pid=<optimized out>, stat_loc=<optimized out>, options=<optimized out>, usage=<optimized out>)");
		std::cout << "Cleaned f2: " << f2 << std::endl;
		assert(f2 == "__GI___wait4 (...)");

		std::string f3 = crashdump_manager::clean_function_signature("foo ()");
		std::cout << "Cleaned f3: " << f3 << std::endl;
		assert(f3 == "foo ()");

		std::string f4 = crashdump_manager::clean_function_signature("main (argc=1, argv=0x7ffedf235548)");
		std::cout << "Cleaned f4: " << f4 << std::endl;
		assert(f4 == "main (argc=1, argv=0x7ffedf235548)");

		std::string f5 = crashdump_manager::clean_function_signature(
			"__assert_fail (assertion=0x562180f73020 \"c != NULL\", file=0x562180f73014 \"../main.cpp\", line=8, function=0x562180f73004 \"void foo(char*)\")");
		std::cout << "Cleaned f5: " << f5 << std::endl;
		assert(f5.find("\"c != NULL\"") != std::string::npos);
		assert(f5.find("\"void foo(char*)\"") != std::string::npos);

		// B. classify_location accurately identifies libc, turbocatch, external, and project paths
		std::string proj_root = project_manager::get_instance().get_project_root();
		std::string build_dir = project_manager::get_instance().resolve_build_dir();

		assert(crashdump_manager::classify_location("./nptl/cancellation.c:44", proj_root, build_dir) == "<libc>");
		assert(crashdump_manager::classify_location("../sysdeps/unix/sysv/linux/wait4.c:30", proj_root, build_dir) == "<libc>");
		assert(crashdump_manager::classify_location("./assert/assert.c:118", proj_root, build_dir) == "<libc>");
		assert(crashdump_manager::classify_location("./stdlib/abort.c:77", proj_root, build_dir) == "<libc>");
		assert(crashdump_manager::classify_location("/lib/x86_64-linux-gnu/libc.so.6", proj_root, build_dir) == "<libc>");
		assert(crashdump_manager::classify_location("../src/crash_catcher/crash_catcher.c:337", proj_root, build_dir) == "<turbocatch>");
		assert(crashdump_manager::classify_location("/opt/custom/libunknown.so", proj_root, build_dir) == "<external>");
		assert(crashdump_manager::classify_location("src/editor.cpp:42", proj_root, build_dir) == "src/editor.cpp:42");
	}

	// Clean up mock files
	crashdump_manager::get_instance().clear_all();
	return 0;
}
