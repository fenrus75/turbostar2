#include <iostream>
#include <cassert>
#include "../../src/agentlib/tool_registry.h"
#include "../../src/agentlib/ai_agent.h"
#include "../../src/project_manager.h"
#include "../../src/config_manager.h"
#include "../../src/git_manager.h"
#include "../../src/event_queue.h"

using namespace agentlib;

int main() {
    // Initialize managers
    project_manager::get_instance().initialize();
    
    tool_registry &registry = tool_registry::get_instance();
    tool_context ctx;
    event_queue q;
    
    ctx.fs_security.set_working_directory(project_manager::get_instance().get_repository_root());
    ctx.fs_security.add_allowed_root(project_manager::get_instance().get_repository_root(), access_type::read);
    ctx.fs_security.add_allowed_root(project_manager::get_instance().get_repository_root(), access_type::write);

    std::cout << "Testing fs_list_tests..." << std::endl;
    std::string list_result = registry.execute_tool("fs_list_tests", "{}", ctx);
    std::cout << "Result:\n" << list_result << std::endl;
    
    assert(list_result.find("Available Tests") != std::string::npos);
    assert(list_result.find("unit_event_logger") != std::string::npos);

    std::cout << "\nTesting fs_run_tests with specific tests..." << std::endl;
    // We'll run a fast test like unit_event_logger
    std::string run_result = registry.execute_tool("fs_run_tests", "{\"test_names\": [\"unit_event_logger\"]}", ctx);
    std::cout << "Result:\n" << run_result << std::endl;
    
    assert(run_result.find("meson test") != std::string::npos);
    assert(run_result.find("unit_event_logger") != std::string::npos);
    assert(run_result.find("OK") != std::string::npos || run_result.find("PASS") != std::string::npos || run_result.find("exit status 0") != std::string::npos);

    std::cout << "\nTesting agent_set_timer..." << std::endl;
    auto model = std::make_shared<ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
    auto agent = ai_agent::create(1, "TestAgent", model, &q, nullptr);
    ctx.active_agent = agent.get();
    std::string timer_result = registry.execute_tool("agent_set_timer", "{\"seconds\": 1}", ctx);
    std::cout << "Result:\n" << timer_result << std::endl;
    assert(timer_result.find("Timer set for 1 seconds.") != std::string::npos);

    std::cout << "\nAll test tools verified!" << std::endl;
    return 0;
}
