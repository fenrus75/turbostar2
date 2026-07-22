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

	auto synth_report = perf_manager::get_instance().parse_and_resolve(tmp_perf_dir.string(), 12345, true);
	assert(synth_report.total_samples == 150);
	std::cout << "Synthetic report total samples: " << synth_report.total_samples << std::endl;

	auto active = perf_manager::get_instance().get_active_profile();
	assert(active.total_samples == 150);

	perf_manager::get_instance().clear_active_profile();
	assert(perf_manager::get_instance().get_active_profile().total_samples == 0);

	// Verify raw synthetic files were cleaned up
	assert(!fs::exists(synth_samples));
	assert(!fs::exists(synth_maps));

	fs::remove_all(tmp_perf_dir);

	std::cout << "perf_manager tests passed successfully!" << std::endl;
	return 0;
}
