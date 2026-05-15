#pragma once

#include "event_queue.h"
#include "menu_bar.h"
#include "status_bar.h"
#include "window.h"
#include "document.h"
#include "dialog.h"
#include <string>
#include <vector>
#include <memory>

/**
 * @brief UI components that can hold focus.
 */
enum class focus_target {
	menu_bar,
	window,
	dialog
};

/**
 * @brief Central controller for the Turbostar editor.
 * 
 * Manages the application lifecycle, UI components, and event dispatching.
 * Implements the focus-based event routing model.
 */
class editor {
public:
	editor(bool debug_mode, const std::string& debug_string, const std::string& filename);
	~editor() = default;

	/**
	 * @brief Main execution loop.
	 */
	void run();

	/**
	 * @brief Changes the current focus target.
	 * @param target The new component to focus.
	 * @param source Optional name of the component initiating the change.
	 */
	void set_focus(focus_target target, const std::string& source = "unknown");

private:
	/**
	 * @brief Routes events from the global queue to the focused component.
	 */
	void dispatch(const editor_event& ev);
	bool handle_k_block_key(int key);
	void render();

	event_queue global_queue_;
	menu_bar top_menu_;
	status_bar bottom_status_;
	
	std::vector<std::shared_ptr<document>> documents_;
	std::vector<std::unique_ptr<window>> windows_;

	focus_target current_focus_{focus_target::window};
	bool k_block_mode_{false};
	
	enum class dialog_mode { none, load, save, search };
	dialog_mode active_dialog_mode_{dialog_mode::none};
	std::unique_ptr<dialog> active_dialog_;

	search_params current_search_;
	bool is_searching_prompt_{false};
	std::string search_input_buffer_;
	
	bool is_running_{true};
	bool debug_mode_{false};
	std::string debug_string_;
};
