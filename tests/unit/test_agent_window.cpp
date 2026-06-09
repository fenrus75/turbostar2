#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "agentlib/ai_agent.h"
#include "event_queue.h"
#include "ui/agent_window.h"

// Mock document provider
class mock_doc_provider : public agentlib::document_provider {
public:
	std::vector<std::string> get_open_document_paths() const override { return {}; }
	std::unique_ptr<agentlib::document_snapshot> get_open_document(const std::string&) const override { return nullptr; }
	bool apply_live_edits(const std::string&, const std::string&) override { return false; }
	void save_all_documents() override {}
};

int main()
{
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

	std::cout << "test_agent_window passed!" << std::endl;
	return 0;
}
