// Tested source file: src/event_queue.cpp
#include <cassert>
#include <iostream>
#include <string>
#include "event_queue.h"
#include "test_watchdog.h"

int main()
{
	test_watchdog::setup_watchdog(10);

	event_queue q;

	// Initially queue must be empty
	assert(!q.pop().has_value());

	// 1. push(event_type)
	q.push(event_type::redraw);
	auto ev1 = q.pop();
	assert(ev1.has_value());
	assert(ev1->type == event_type::redraw);
	assert(ev1->key_code == 0);
	assert(ev1->payload.empty());

	// 2. push(event_type, int key_code)
	q.push(event_type::select_window, 42);
	auto ev2 = q.pop();
	assert(ev2.has_value());
	assert(ev2->type == event_type::select_window);
	assert(ev2->key_code == 42);
	assert(ev2->payload.empty());

	// 3. push(event_type, std::string_view payload)
	q.push(event_type::open_file, "path/to/test.txt");
	auto ev3 = q.pop();
	assert(ev3.has_value());
	assert(ev3->type == event_type::open_file);
	assert(ev3->key_code == 0);
	assert(ev3->payload == "path/to/test.txt");

	// 4. push(event_type, int key_code, std::string_view payload)
	q.push(event_type::agent_response, 999, "Agent completed turn");
	auto ev4 = q.pop();
	assert(ev4.has_value());
	assert(ev4->type == event_type::agent_response);
	assert(ev4->key_code == 999);
	assert(ev4->payload == "Agent completed turn");

	// 5. Designated initializer (Approach A) with multiple fields
	q.push({.type = event_type::mouse_click, .mouse_x = 15, .mouse_y = 25});
	auto ev5 = q.pop();
	assert(ev5.has_value());
	assert(ev5->type == event_type::mouse_click);
	assert(ev5->mouse_x == 15);
	assert(ev5->mouse_y == 25);

	// 6. Move semantics
	editor_event move_ev{.type = event_type::apply_edits, .payload = "some payload"};
	q.push(std::move(move_ev));
	auto ev6 = q.pop();
	assert(ev6.has_value());
	assert(ev6->type == event_type::apply_edits);
	assert(ev6->payload == "some payload");

	// 7. FIFO ordering with multiple items
	q.push(event_type::compile);
	q.push(event_type::next_error, 1);
	q.push(event_type::quit);

	auto first = q.pop();
	assert(first.has_value() && first->type == event_type::compile);
	auto second = q.pop();
	assert(second.has_value() && second->type == event_type::next_error && second->key_code == 1);
	auto third = q.pop();
	assert(third.has_value() && third->type == event_type::quit);
	assert(!q.pop().has_value());

	std::cout << "All event_queue tests passed!\n";
	return 0;
}
