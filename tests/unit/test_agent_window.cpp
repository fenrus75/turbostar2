#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include <ncurses.h>
#include "agentlib/ai_agent.h"
#include "event_queue.h"
#include "ui/agent_window.h"

// Mock document provider
class mock_doc_provider : public agentlib::document_provider
{
      public:
	std::vector<std::string> get_open_document_paths() const override
	{
		return {};
	}
	std::unique_ptr<agentlib::document_snapshot> get_open_document(const std::string &) const override
	{
		return nullptr;
	}
	bool apply_live_edits(const std::string &, const std::string &) override
	{
		return false;
	}
	void save_all_documents() override
	{
	}
};

int main()
{
	test_watchdog::setup_watchdog(30);
	event_queue queue;
	mock_doc_provider doc_prov;

	// Create a dummy model and agent
	auto model = std::make_shared<agentlib::ai_model>("dummy", "dummy", "url", "purpose", 0.0, 0.0);

	// Instantiate the agent window
	agent_window win(1, 0, 0, 80, 24, model, queue, &doc_prov, true);

	auto agent = win.get_agent();

	// Add 100 dummy system messages to force the window content height > 24
	for (int i = 0; i < 100; i++) {
		auto msg = std::make_shared<agentlib::interaction_system_message>("Line " + std::to_string(i));
		agent->add_interaction(msg);
	}

	// Trigger a redraw so it calculates max_scroll_offset_
	win.draw_content(false);

	// Try scrolling up
	editor_event ev_up;
	ev_up.type = event_type::mouse_scroll_up;
	ev_up.mouse_x = 40; // Inside the window
	ev_up.mouse_y = 10;

	int initial_offset = win.get_scroll_offset();

	win.get_window_queue().push(ev_up);
	win.process_events();

	int after_up_offset = win.get_scroll_offset();
	assert(after_up_offset > initial_offset);

	// Try scrolling down
	editor_event ev_down;
	ev_down.type = event_type::mouse_scroll_down;
	ev_down.mouse_x = 40;
	ev_down.mouse_y = 10;

	win.get_window_queue().push(ev_down);
	win.process_events();

	int after_down_offset = win.get_scroll_offset();
	assert(after_down_offset < after_up_offset);
	assert(after_down_offset == initial_offset);

	// 1. Test Content-Lock
	// Scroll up once to get a non-zero offset
	win.get_window_queue().push(ev_up);
	win.process_events();
	int offset_before_new_msg = win.get_scroll_offset();
	assert(offset_before_new_msg > 0);

	// Add more messages to increase the history height
	for (int i = 100; i < 110; i++) {
		auto msg = std::make_shared<agentlib::interaction_system_message>("New line " + std::to_string(i));
		agent->add_interaction(msg);
	}

	// Trigger a redraw so it calculates total turns height and adjusts offset
	win.draw_content(false);
	int offset_after_new_msg = win.get_scroll_offset();

	// Scroll offset must have increased to keep the visible content static
	assert(offset_after_new_msg > offset_before_new_msg);

	// 2. Test on_agent_update follow mode snapping logic
	// If offset is > 0, on_agent_update() should NOT reset it to 0
	win.on_agent_update();
	assert(win.get_scroll_offset() > 0);

	// If offset is 0, on_agent_update() should keep it at 0
	editor_event ev_end;
	ev_end.type = event_type::key_press;
	ev_end.key_code = KEY_END;
	win.get_window_queue().push(ev_end);
	win.process_events();
	assert(win.get_scroll_offset() == 0);

	win.on_agent_update();
	assert(win.get_scroll_offset() == 0);

	// 3. Test that typing in the input box snaps back to the bottom
	// Scroll up again
	win.get_window_queue().push(ev_up);
	win.process_events();
	assert(win.get_scroll_offset() > 0);

	// Activate window so that it accepts key input
	win.set_active(true);
	win.draw_content(false);

	// Send keypress handled by input box
	editor_event ev_type;
	ev_type.type = event_type::key_press;
	ev_type.key_code = 'x';
	win.get_window_queue().push(ev_type);
	win.process_events();

	// Any typing in the input box should snap scroll_offset_ back to 0
	assert(win.get_scroll_offset() == 0);

	// 4. Test clicking the follow overlay snaps back to the bottom
	// Scroll up again
	win.get_window_queue().push(ev_up);
	win.process_events();
	assert(win.get_scroll_offset() > 0);

	// Trigger draw_content to ensure overlay coordinates are computed
	win.draw_content(false);

	// Click follow button coordinates
	// overlay_y = 0 + 24 - 6 = 18
	// max_width = 78 (width_ - 2)
	// overlay_x = 0 + 78 - 12 = 66
	editor_event ev_click;
	ev_click.type = event_type::mouse_click;
	ev_click.mouse_x = 68; // inside bounds [66, 75]
	ev_click.mouse_y = 18;

	win.get_window_queue().push(ev_click);
	win.process_events();

	assert(win.get_scroll_offset() == 0);

	std::cout << "test_agent_window passed!" << std::endl;
	return 0;
}
