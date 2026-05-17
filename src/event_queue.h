#pragma once

#include <mutex>
#include <optional>
#include <queue>
#include <string>

/**
 * @brief Event types supported by the editor.
 */
enum class event_type {
	key_press,    ///< A keyboard input event
	quit,	      ///< An application exit event
	load,	      ///< Request to load a file (triggers dialog)
	save,	      ///< Request to save a file (smart save)
	save_as,      ///< Request to save as (always triggers dialog)
	new_doc,      ///< Request to clear current document
	about,	      ///< Request to show About dialog
	redraw,	      ///< Request a global UI re-render
	find,	      ///< Request to find text (triggers dialog)
	replace,      ///< Request to replace text (triggers dialog)
	format_doc,   ///< Request to format document/range
	settings,     ///< Request to show settings dialog
	git_status_updated, ///< Notification that git status has changed
	git_add,      ///< Request to git-add current file
	git_refresh,  ///< Request to refresh git status manually
	compile,      ///< Request to run the compile command
	select_window, ///< Request to switch active window (key_code is index)
	lsp_hover_result, ///< Notification that LSP hover information is available
	lsp_highlight_result, ///< Notification that LSP document highlight is available
	lsp_selection_range_result, ///< Notification that LSP selection range is available
	lsp_diagnostics_result ///< Notification that LSP diagnostics are available
};

struct text_range {
	int start_y;
	int start_x;
	int end_y;
	int end_x;
};

struct diagnostic_info {
	text_range range;
	int severity; // 1: Error, 2: Warning, 3: Info, 4: Hint
	std::string message;
};

/**
 * @brief Represents a single event in the editor system.
 *
 * The event model follows a two-tier dispatch system:
 * 1. Global Queue: All raw input events (keys, system events) are pushed here.
 * 2. Central Dispatcher: Processes the Global Queue and routes events to the
 *    focused component's local queue or handler.
 * 3. Local/Window Queue: Components (like windows) have their own queues for
 *    asynchronous processing of routed events.
 */
struct editor_event {
	event_type type;
	int key_code{0};       ///< NCurses key code or ASCII value
	std::string utf8_char; ///< UTF-8 character sequence for typing
	std::string payload;   ///< General payload for complex events (like LSP results)
	std::vector<text_range> highlight_ranges; ///< Payload for LSP highlights
	std::vector<diagnostic_info> diagnostics; ///< Payload for LSP diagnostics
};

/**
 * @brief Thread-safe event queue for passing messages between components and
 * threads.
 */
class event_queue
{
      public:
	event_queue() = default;
	~event_queue() = default;

	void push(const editor_event &ev);
	std::optional<editor_event> pop();

      private:
	std::queue<editor_event> queue_;
	mutable std::mutex mutex_;
};
