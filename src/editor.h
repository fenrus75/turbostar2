#pragma once

#include "event_queue.h"
#include "menu_bar.h"
#include "status_bar.h"
#include <string>

class editor {
public:
	editor(bool debug_mode, const std::string& debug_string);
	~editor() = default;

	void run();

private:
	void dispatch(const editor_event& ev);
	void render();

	event_queue global_queue_;
	menu_bar top_menu_;
	status_bar bottom_status_;

	bool is_running_{true};
	bool debug_mode_{false};
	std::string debug_string_;
};
