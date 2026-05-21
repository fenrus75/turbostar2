#include <iostream>
#include <cassert>
#include <memory>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/agentlib/replay_transport.h"
#include "../../src/event_queue.h"

int main() {
    event_queue q;
    auto agent = agentlib::ai_agent::create(1, "TestAgent", "tests/data/todo_traffic.json", &q, nullptr);
    
    agent->submit_prompt("Add a task to read the Readme, then list my tasks. Then mark the first one complete using the tools available.");
    
    // Process events until the agent_response arrives
    bool done = false;
    while (!done) {
        if (auto ev = q.pop()) {
            if (ev->type == event_type::agent_response) {
                std::cout << "Response: " << ev->payload << std::endl;
                done = true;
            } else if (ev->type == event_type::agent_tool_update) {
                std::cout << "Tool: " << ev->payload << std::endl;
            }
        }
    }
    
    auto todos = agent->get_todos();
    assert(todos.size() == 1);
    assert(todos[0].text == "Read the Readme file");
    assert(todos[0].completed == true);
    
    std::cout << "To-Do management replay test passed successfully!" << std::endl;
    return 0;
}
