#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "../../src/address_lookup.h"

using namespace turbostar;

// Simple dummy function to resolve via address_lookup in test
void sample_test_function()
{
	std::cout << "Sample test function for address resolution" << std::endl;
}

int main()
{
	test_watchdog::setup_watchdog(30);

	std::cout << "Testing address_lookup..." << std::endl;

	// 1. Test maps parsing for self
	auto maps = address_lookup::parse_maps("/proc/self/maps");
	assert(!maps.empty());
	std::cout << "Parsed " << maps.size() << " executable memory mappings for self." << std::endl;

	// 2. Test single address resolution on function pointer
	uintptr_t func_addr = reinterpret_cast<uintptr_t>(&sample_test_function);
	auto res_single = address_lookup::resolve_address(func_addr, "/proc/self/maps");
	std::cout << "Single resolution for " << std::hex << func_addr << " -> Func: "
		  << res_single.function_name << " Loc: " << res_single.location << std::endl;
	assert(res_single.address == func_addr);

	// 3. Test batch address resolution with multiple IPs (including duplicates)
	uintptr_t second_addr = func_addr + 0x10;
	std::vector<uintptr_t> batch_addrs = {func_addr, second_addr, func_addr, 0x123456};

	auto batch_results = address_lookup::resolve_addresses(batch_addrs, "/proc/self/maps");
	assert(batch_results.size() == batch_addrs.size());
	assert(batch_results[0].address == func_addr);
	assert(batch_results[1].address == second_addr);
	assert(batch_results[2].address == func_addr);
	assert(batch_results[3].address == 0x123456);

	std::cout << "Batch resolution returned " << batch_results.size() << " items successfully." << std::endl;
	std::cout << "address_lookup tests passed successfully!" << std::endl;
	return 0;
}
