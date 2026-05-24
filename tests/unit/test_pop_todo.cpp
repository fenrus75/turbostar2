#include <cassert>
#include <iostream>
#include <memory>
#include "../../src/agentlib/ai_agent.h"
#include "../../src/event_queue.h"

int main()
{
	event_queue q;
	auto model = std::make_shared<agentlib::ai_model>("test-model", "Test Model", "http://localhost", "Test", 0.0, 0.0);
	auto agent = agentlib::ai_agent::create(1, "TestAgent", model, &q, nullptr);

	agent->add_todo("Task 1");
	agent->add_todo("Task 2");

	auto todos = agent->get_todos();
	assert(todos.size() == 2);
	assert(todos[0].text == "Task 1");
	assert(todos[1].text == "Task 2");

	auto popped1 = agent->pop_todo();
	assert(popped1.has_value());
	assert(*popped1 == "Task 1");

	todos = agent->get_todos();
	assert(todos.size() == 1);
	assert(todos[0].text == "Task 2");

	auto popped2 = agent->pop_todo();
	assert(popped2.has_value());
	assert(*popped2 == "Task 2");

	auto popped3 = agent->pop_todo();
	assert(!popped3.has_value());

	std::cout << "pop_todo unit test passed successfully!" << std::endl;
	return 0;
}
