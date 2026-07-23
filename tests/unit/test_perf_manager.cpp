#include "test_watchdog.h"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../../src/perf_manager.h"

namespace fs = std::filesystem;
using namespace turbostar;

// Dummy test function for symbol resolution
void dummy_target_function()
{
	std::cout << "Dummy function for perf resolution testing" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing perf_manager..." << std::endl;

	fs::path tmp_perf_dir = fs::current_path() / "tests" / "unit" / "tmp_perf_mgr_test";
	fs::create_directories(tmp_perf_dir);

	// 1. Synthetic sample file testing
	fs::path synth_samples = tmp_perf_dir / "perf_samples_12345.dat";
	fs::path synth_maps = tmp_perf_dir / "perf_maps_12345.txt";

	// Copy self maps
	std::ifstream self_maps("/proc/self/maps");
	std::ofstream maps_out(synth_maps);
	maps_out << self_maps.rdbuf();
	self_maps.close();
	maps_out.close();

	struct raw_slot {
		unsigned long ip;
		unsigned long count;
	};

	uintptr_t fn_ip = reinterpret_cast<uintptr_t>(&dummy_target_function);
	raw_slot slot1{fn_ip, 100};
	raw_slot slot2{fn_ip + 0x10, 50};

	std::ofstream samples_out(synth_samples, std::ios::binary);
	samples_out.write(reinterpret_cast<const char *>(&slot1), sizeof(slot1));
	samples_out.write(reinterpret_cast<const char *>(&slot2), sizeof(slot2));
	samples_out.close();

	auto synth_report = perf_manager::get_instance().parse_and_resolve(tmp_perf_dir.string(), 12345, "editor", true);
	assert(synth_report.total_samples == 150);
	std::cout << "Synthetic report total samples: " << synth_report.total_samples << std::endl;

	auto active = perf_manager::get_instance().get_active_profile();
	assert(active.total_samples == 150);

	// 2. Multi-Run Profile Storage & Comparison via run_id Tests
	perf_profile_report run1_rep;
	run1_rep.total_samples = 500;
	perf_line_sample r1_ls{"main.cpp", 15, 0, "slow_func()", 400, 80.0};
	run1_rep.top_lines = {r1_ls};
	run1_rep.line_samples_by_file["main.cpp"] = {r1_ls};

	perf_profile_report run2_rep;
	run2_rep.total_samples = 100;
	perf_line_sample r2_ls{"main.cpp", 15, 0, "slow_func()", 20, 20.0};
	run2_rep.top_lines = {r2_ls};
	run2_rep.line_samples_by_file["main.cpp"] = {r2_ls};

	perf_manager::get_instance().set_active_profile(run1_rep, "run_1");
	perf_manager::get_instance().set_active_profile(run2_rep, "run_2");

	// Active profile should be latest ("run_2")
	assert(perf_manager::get_instance().get_active_profile().total_samples == 100);

	// Query run_1 specifically by "run_1" and by numeric string "1"
	auto fetched_run1 = perf_manager::get_instance().get_profile_for_run("run_1");
	assert(fetched_run1.total_samples == 500);
	assert(fetched_run1.top_lines[0].percentage == 80.0);

	auto fetched_run1_num = perf_manager::get_instance().get_profile_for_run("1");
	assert(fetched_run1_num.total_samples == 500);

	// Query run_2 specifically by "run_2"
	auto fetched_run2 = perf_manager::get_instance().get_profile_for_run("run_2");
	assert(fetched_run2.total_samples == 100);
	assert(fetched_run2.top_lines[0].percentage == 20.0);

	// 3. Editor UI Query & Document Listener Interface Tests
	perf_profile_report test_rep;
	test_rep.total_samples = 1000;

	perf_line_sample ls1{"test_file.cpp", 10, 5, "foo_func()", 600, 60.0};
	perf_line_sample ls2{"test_file.cpp", 20, 12, "bar_func()", 100, 10.0};
	test_rep.line_samples_by_file["test_file.cpp"] = {ls1, ls2};
	test_rep.top_lines = {ls1, ls2};

	perf_manager::get_instance().set_active_profile(test_rep);

	assert(perf_manager::get_instance().go_to_hotspot_possible());

	perf_line_sample next_hs;
	// Not on a hotspot line -> jumps to #1 (ls1, line 10)
	assert(perf_manager::get_instance().get_next_hotspot("other_file.cpp", 1, next_hs));
	assert(next_hs.line_number == 10 && next_hs.column_number == 5);

	// On Hotspot #1 (ls1, line 10) -> jumps to #2 (ls2, line 20)
	assert(perf_manager::get_instance().get_next_hotspot("test_file.cpp", 10, next_hs));
	assert(next_hs.line_number == 20 && next_hs.column_number == 12);

	// On Hotspot #2 (ls2, line 20) -> wraps to #1 (ls1, line 10)
	assert(perf_manager::get_instance().get_next_hotspot("test_file.cpp", 20, next_hs));
	assert(next_hs.line_number == 10 && next_hs.column_number == 5);

	// Verify initial queries
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 10) == 60.0);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 20) == 10.0);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 15) == 0.0);

	std::string status10 = perf_manager::get_instance().get_line_profile_statusmsg("test_file.cpp", 10);
	assert(status10 == "Perf: 60.0% (600 samples)");

	assert(perf_manager::get_instance().get_line_profile_statusmsg("test_file.cpp", 15).empty());

	// Verify line insertion at line 5 (0-indexed y = 4) -> line 10 becomes 11, line 20 becomes 21
	perf_manager::get_instance().on_line_inserted("test_file.cpp", 4);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 11) == 60.0);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 21) == 10.0);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 10) == 0.0);

	// Verify line deletion at line 21 (0-indexed y = 20) -> line 21 deleted
	perf_manager::get_instance().on_line_deleted("test_file.cpp", 20);
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 21) == 0.0);

	// Verify edit count invalidation threshold (18 more edits = 20 total)
	for (int i = 0; i < 18; ++i) {
		perf_manager::get_instance().on_line_inserted("test_file.cpp", 1);
	}
	assert(!perf_manager::get_instance().is_file_profile_valid("test_file.cpp"));
	assert(perf_manager::get_instance().get_line_profile_percentage("test_file.cpp", 11) == 0.0);
	assert(perf_manager::get_instance().get_line_profile_statusmsg("test_file.cpp", 11).empty());

	perf_manager::get_instance().clear_active_profile();
	assert(perf_manager::get_instance().get_active_profile().total_samples == 0);

	// Verify raw synthetic files were cleaned up
	assert(!fs::exists(synth_samples));
	assert(!fs::exists(synth_maps));

	fs::remove_all(tmp_perf_dir);

	std::cout << "perf_manager tests passed successfully!" << std::endl;
	return 0;
}
