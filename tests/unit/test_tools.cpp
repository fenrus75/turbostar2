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

    std::cout << "\nTesting get_memory_index..." << std::endl;
    std::string empty_mem = agent->get_memory_index();
    std::cout << "Empty memory index:\n" << empty_mem << std::endl;
    assert(empty_mem.find("Memory index is empty") != std::string::npos);

    agent->inject_context("user", "Hello world");
    agent->inject_context("assistant", "Hi there!");
    agent->snapshot_episode("Test Episode", "Test Summary", {"test-tag"});

    std::string populated_mem = agent->get_memory_index();
    std::cout << "Populated memory index:\n" << populated_mem << std::endl;
    assert(populated_mem.find("Agent Memory Index") != std::string::npos);
    assert(populated_mem.find("Test Episode") != std::string::npos);
    assert(populated_mem.find("raw") != std::string::npos);
    assert(populated_mem.find("think-free") != std::string::npos);
    assert(populated_mem.find("think-free+pseudo") != std::string::npos);

    std::cout << "\nTesting set_episode_state (paging in, shifting levels, and evicting)..." << std::endl;
    // Page out the turns to create episode_2
    agent->page_out_context(0, 2, "Manual Episode", "Manual Episode Summary", {"manual-tag"});
    
    auto convo_after_out = agent->get_conversation();
    bool found_anchor = false;
    for (const auto& msg : convo_after_out) {
        if (msg.role == "system" && msg.content.find("Raw history archive: episode_2") != std::string::npos) {
            found_anchor = true;
        }
    }
    assert(found_anchor);

    // Page in episode_2 at level 1
    bool pagein_ok = agent->set_episode_state("episode_2", 1);
    assert(pagein_ok);

    auto convo_after_in = agent->get_conversation();
    bool found_paged_in_turns = false;
    for (const auto& msg : convo_after_in) {
        if (msg.episode_id == "episode_2") {
            assert(msg.episode_level == 1);
            found_paged_in_turns = true;
        }
    }
    assert(found_paged_in_turns);

    // Shift level to level 2
    bool shift_ok = agent->set_episode_state("episode_2", 2);
    assert(shift_ok);

    auto convo_after_shift = agent->get_conversation();
    bool found_shifted_turns = false;
    for (const auto& msg : convo_after_shift) {
        if (msg.episode_id == "episode_2") {
            assert(msg.episode_level == 2);
            found_shifted_turns = true;
        }
    }
    assert(found_shifted_turns);

    // Evict (page out) episode_2
    bool evict_ok = agent->set_episode_state("episode_2", 99);
    assert(evict_ok);

    auto convo_after_evict = agent->get_conversation();
    bool found_anchor_again = false;
    for (const auto& msg : convo_after_evict) {
        // Turns should be gone (no msg should have episode_id == "episode_2")
        assert(msg.episode_id != "episode_2");
        if (msg.role == "system" && msg.content.find("Raw history archive: episode_2") != std::string::npos) {
            found_anchor_again = true;
        }
    }
    assert(found_anchor_again);
    std::cout << "Episode state machine transitions verified successfully!" << std::endl;

    std::cout << "\nAll test tools verified!" << std::endl;
    return 0;
}
