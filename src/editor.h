#pragma once

#include <memory>
#include <string>
#include <vector>
#include "dialog.h"
#include "document.h"
#include "event_queue.h"
#include "menu_bar.h"
#include "status_bar.h"
#include "window.h"
#include "process_runner.h"

/**
 * @brief UI components that can hold focus.
 */
enum class focus_target { menu_bar, window, dialog };

/**
 * @brief Central controller for the Turbostar editor.
 *
 * Manages the application lifecycle, UI components, and event dispatching.
 * Implements the focus-based event routing model.
 */
class editor
{
      public:
	editor(bool debug_mode, const std::string &debug_string, const std::vector<std::string> &filenames, bool exit_immediately, bool no_lsp = false);
	~editor();

	/**
	 * @brief Main execution loop.
	 */
	void run();

	/**
	 * @brief Changes the current focus target.
	 * @param target The new component to focus.
	 * @param source Optional name of the component initiating the change.
	 */
	void set_focus(focus_target target, const std::string &source = "unknown");

      private:
	void new_window(const std::string &filename);
	void activate_window(size_t index);
	void update_window_menu();
	std::shared_ptr<document> get_active_doc() const;
	window *get_active_window() const;

	/**
	 * @brief Routes events from the global queue to the focused component.
	 */
	void dispatch(const editor_event &ev);
	void dispatch_event_mouse(const editor_event &ev);
	void dispatch_event_ui(const editor_event &ev);
	void dispatch_event_file(const editor_event &ev);
	void dispatch_event_window(const editor_event &ev);
	void dispatch_event_search(const editor_event &ev);
	void dispatch_event_git(const editor_event &ev);
	void dispatch_event_build(const editor_event &ev);
	void dispatch_event_lsp(const editor_event &ev);
	void dispatch_event_key(const editor_event &ev);
	bool handle_k_block_key(int key);
	bool handle_q_block_key(int key);
	void render();

	event_queue global_queue_;
	menu_bar top_menu_;
	status_bar bottom_status_;

	std::vector<std::shared_ptr<document>> documents_;
	std::vector<std::unique_ptr<window>> windows_;

	focus_target current_focus_{focus_target::window};
	bool k_block_mode_{false};
	bool q_block_mode_{false};

	enum class dialog_mode { none, load, save, search, replace, insert_file, settings };
	dialog_mode active_dialog_mode_{dialog_mode::none};
	std::unique_ptr<dialog> active_dialog_;

	search_params current_search_;
	bool is_searching_prompt_{false};
	std::string search_input_buffer_;
	
	bool is_search_options_prompt_{false};
	std::string search_options_buffer_;

	bool is_going_to_line_prompt_{false};
	std::string line_input_buffer_;
	
	std::string hover_text_;

	std::string get_search_autocomplete() const;

	bool is_running_{true};
	bool exit_immediately_{false};
	bool debug_mode_{false};
	std::string debug_string_;

	std::unique_ptr<process_runner> current_build_process_;
};
