// Tested source file: src/agentlib/agent_run_registry.cpp
#include <cassert>
#include <iostream>
#include <string>
#include "agentlib/agent_run_registry.h"
#include "test_watchdog.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog();

	std::cout << "Testing agent_run_registry basic registration and queries..." << std::endl;
	agent_run_registry reg;
	assert(reg.size() == 0);
	assert(!reg.get_latest_run_id().has_value());
	assert(!reg.get_latest_app_id().has_value());
	assert(!reg.get_latest_gdb_id().has_value());

	// Register single app run
	reg.register_app_run(1001, "build/turbostar");
	assert(reg.size() == 1);
	assert(reg.get_latest_run_id() == 1001);
	assert(reg.get_latest_app_id() == 1001);
	assert(!reg.get_latest_gdb_id().has_value());

	// Register paired app + gdb run
	reg.register_app_run(1010, "build/test_app", 1011);
	reg.register_gdb_run(1011, 1010);
	assert(reg.size() == 3);
	assert(reg.get_latest_run_id() == 1011);
	assert(reg.get_latest_app_id() == 1010);
	assert(reg.get_latest_gdb_id() == 1011);
	assert(reg.get_linked_run_id(1010) == 1011);
	assert(reg.get_linked_run_id(1011) == 1010);

	std::cout << "Testing symbolic alias resolution..." << std::endl;
	// "gdb" and "debugger"
	assert(reg.resolve_run_id("gdb") == 1011);
	assert(reg.resolve_run_id("GDB") == 1011);
	assert(reg.resolve_run_id("debugger") == 1011);

	// "app" and "main"
	assert(reg.resolve_run_id("app") == 1010);
	assert(reg.resolve_run_id("APP") == 1010);
	assert(reg.resolve_run_id("main") == 1010);

	// "latest"
	assert(reg.resolve_run_id("latest") == 1011);

	// Formatted integers
	assert(reg.resolve_run_id("1001") == 1001);
	assert(reg.resolve_run_id("run_1010") == 1010);
	assert(reg.resolve_run_id("RUN_1010") == 1010);

	// Omitted / nullopt defaults
	// preferred_kind::debugger should default to 1011
	assert(reg.resolve_run_id(std::nullopt, run_target_kind::debugger) == 1011);
	// preferred_kind::app should default to 1010
	assert(reg.resolve_run_id(std::nullopt, run_target_kind::app) == 1010);

	std::cout << "Testing resolve_from_json..." << std::endl;
	int resolved_id = -1;
	std::string err;

	// 1. Integer in JSON
	assert(reg.resolve_from_json(nlohmann::json{{"run_id", 1001}}, run_target_kind::app, resolved_id, err));
	assert(resolved_id == 1001);

	// 2. String alias in JSON
	assert(reg.resolve_from_json(nlohmann::json{{"run_id", "gdb"}}, run_target_kind::app, resolved_id, err));
	assert(resolved_id == 1011);

	// 3. Omitted run_id in JSON with debugger preference
	assert(reg.resolve_from_json(nlohmann::json{{"data", "bt\n"}}, run_target_kind::debugger, resolved_id, err));
	assert(resolved_id == 1011);

	// 4. Omitted run_id in JSON with app preference
	assert(reg.resolve_from_json(nlohmann::json{{"timeout", 10}}, run_target_kind::app, resolved_id, err));
	assert(resolved_id == 1010);

	// 5. Invalid alias string
	assert(!reg.resolve_from_json(nlohmann::json{{"run_id", "nonexistent_alias"}}, run_target_kind::app, resolved_id, err));
	assert(err.find("unknown alias") != std::string::npos);

	std::cout << "Testing teardown tracking and double-termination handling..." << std::endl;
	assert(!reg.is_already_terminated(1010));
	assert(!reg.is_already_terminated(1011));

	// Terminate 1010 -> should return both 1010 and 1011
	auto terminated = reg.mark_terminated(1010);
	assert(terminated.size() == 2);
	assert(reg.is_already_terminated(1010));
	assert(reg.is_already_terminated(1011));

	// Querying alive IDs after termination
	assert(reg.get_latest_app_id(true) == 1001);	  // 1001 is still alive
	assert(!reg.get_latest_gdb_id(true).has_value()); // No alive GDB

	// Resolution for omitted preferred debugger falls back to any known or alive app
	assert(reg.resolve_run_id(std::nullopt, run_target_kind::debugger) == 1001);

	std::cout << "All agent_run_registry unit tests passed successfully!" << std::endl;
	return 0;
}
