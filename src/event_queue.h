#pragma once

#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

/**
 * @brief Event types supported by the editor.
 *
 * IMPORTANT DEVELOPER / AI AGENT NOTE:
 * If you add a new event type to this enum, you MUST update the central routing
 * switch statement in editor::dispatch() inside src/editor_events.cpp to map
 * the event to its appropriate handler. Missing this mapping will compile cleanly
 * with NO warnings (due to the default: break; case) but will cause the event
 * to be silently discarded at runtime.
 */
enum class event_type {
	key_press,		    ///< A keyboard input event
	quit,			    ///< An application exit event (with prompts)
	force_quit,		    ///< An immediate application exit event
	load,			    ///< Request to load a file (triggers dialog)
	save,			    ///< Request to save a file (smart save)
	save_as,		    ///< Request to save as (always triggers dialog)
	save_all,		    ///< Request to save all modified documents
	new_doc,		    ///< Request to clear current document
	revert,			    ///< Request to revert current document to saved state or clean dirty flag
	about,			    ///< Request to show About dialog
	redraw,			    ///< Request a global UI re-render
	find,			    ///< Request to find text (triggers dialog)
	replace,		    ///< Request to replace text (triggers dialog)
	format_doc,		    ///< Request to format document/range
	settings,		    ///< Request to show settings dialog
	models_config,		    ///< Request to show AI models configuration dialog
	help,			    ///< Request to show help window
	git_status_updated,	    ///< Notification that git status has changed
	git_add,		    ///< Request to git-add current file
	git_refresh,		    ///< Request to refresh git status manually
	compile,		    ///< Request to run the compile command
	compile_file,		    ///< Request to compile only the active file
	run_tests,		    ///< Request to run the test suite
	next_error,		    ///< Request to jump to the next build error (F4)
	close_window,		    ///< Request to close the active window
	maximize_window,	    ///< Request to maximize/restore the active window
	select_window,		    ///< Request to switch active window (key_code is index)
	lsp_hover_result,	    ///< Notification that LSP hover information is available
	lsp_highlight_result,	    ///< Notification that LSP document highlight is available
	lsp_selection_range_result, ///< Notification that LSP selection range is available
	lsp_diagnostics_result,	    ///< Notification that LSP diagnostics are available
	mouse_click,		    ///< A mouse click event
	mouse_scroll_up,	    ///< Mouse scroll up event
	mouse_scroll_down,	    ///< Mouse scroll down event
	mouse_release,		    ///< Mouse button release event
	mouse_drag,		    ///< Mouse drag event
	agent_response,		    ///< Notification that LLM has responded
	agent_tool_update,	    ///< Notification that LLM is executing a tool
	open_agent,		    ///< Request to open the LLM agent chat window
	open_subagent,		    ///< Request to open a specific subagent chat window (key_code is agent ID)
	open_crashdump_viewer,	    ///< Request to open the crashdump viewer
	agent_switch_model,	    ///< Request to switch the model for a specific agent
	agent_save_history,	    ///< Request to save the active agent's history to a JSON file
	apply_edits,		    ///< Request to apply JSON-serialized LLM edits to the live document
	prompt_user,		    ///< Request to prompt the user with a question and options
	approve_plan,		    ///< Request to prompt the user to approve a plan
	paste,			    ///< A bracketed paste event
	set_transient_status,	    ///< Set a temporary message in the status bar
	inline_agent_request,	    ///< Request for a headless agent operation
	open_file,		    ///< Request to open a file in the editor
	run_program,		    ///< Request to run the main program executable
	run_settings,		    ///< Request to show run settings/options dialog
	run_in_debugger,	    ///< Request to run the main program in debugger
	tool_status,		    ///< Request to show the Tool status dialog
	notify_undo_changed,	    ///< Notification that the undo stack has changed
	mcp_config,		    ///< Request to show MCP configuration dialog
	terminate_run,		    ///< Request to terminate a run
	agent_start_app		    ///< Request to start app from agent
};

namespace status_priorities
{
constexpr int LOWEST = 0;
constexpr int HOVER = 10;
constexpr int INFO = 20;
constexpr int WARNING = 30;
constexpr int CRITICAL = 40;
} // namespace status_priorities

struct text_range {
	int start_y;
	int start_x;
	int end_y;
	int end_x;
};

struct build_error {
	std::string filepath;
	int line;
	int column;
	int end_column; // Optional: 0 means highlight the whole line
	std::string message;
	bool is_warning;
	int output_buffer_line; // Line in the "Compile Output" doc where this error was found
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
	int mouse_x{-1};       ///< Mouse X coordinate (if type == mouse_click)
	int mouse_y{-1};       ///< Mouse Y coordinate (if type == mouse_click)
	std::string utf8_char; ///< UTF-8 character sequence for typing
	bool alt_pressed{false};
	std::string payload;			  ///< General payload for complex events (like LSP results)
	std::vector<text_range> highlight_ranges; ///< Payload for LSP highlights
	std::vector<diagnostic_info> diagnostics; ///< Payload for LSP diagnostics
	int priority{0};			  ///< Status message priority (0 = lowest)

	// Payload for prompt_user
	std::vector<std::string> prompt_options;
	std::shared_ptr<std::promise<std::string>> prompt_promise;
	std::shared_ptr<void> generic_promise;
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
