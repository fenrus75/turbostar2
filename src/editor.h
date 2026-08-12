#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "agentlib/document_provider.h"
#include "document.h"
#include "event_queue.h"
#include "process_runner.h"
#include "ui/dialog.h"
#include "ui/menu_bar.h"
#include "ui/popup_menu.h"
#include "ui/status_bar.h"
#include "ui/terminal_window.h"

namespace agentlib
{
class ai_agent;
}

/**
 * @brief UI components that can hold focus.
 */
enum class focus_target { menu_bar, window, dialog, popup };

std::string focus_target_to_string(focus_target target);

/**
 * @brief Central controller for the Turbostar editor.
 *
 * Manages the application lifecycle, UI components, and event dispatching.
 * Implements the focus-based event routing model.
 */
struct editor_options {
	bool debug_mode = false;
	std::string debug_string;
	std::vector<std::string> filenames;
	double exit_immediately = -1.0;
	bool no_lsp = false;
	bool no_welcome = false;
	std::string initial_agent_prompt;
	bool fresh_agent = false;
	bool start_with_agent = false;
};

class editor : public agentlib::document_provider
{
      public:
	struct latency_spike {
		double duration_ms;
		int key_code;
		std::string key_desc;
		std::vector<std::string> trace;
	};

	explicit editor(editor_options opts);
	~editor();

	friend void test_vim_emulation();
	friend void test_status_bar_paste();

#include <string_view>

	const std::vector<latency_spike> &get_latency_spikes_for_testing() const noexcept
	{
		return latency_spikes_;
	}
	void add_latency_spike_for_testing(const latency_spike &spike)
	{
		latency_spikes_.push_back(spike);
	}

	/**
	 * @brief Main execution loop.
	 */
	void run();

	/**
	 * @brief Saves the current search parameters to the project config for persistence.
	 */
	void save_search_persistence();

	/**
	 * @brief Prints the interactive event response latency metrics to the console.
	 */
	void print_latency_report() const;

	/**
	 * @brief Changes the current focus target.
	 * @param target The new component to focus.
	 * @param source Optional name of the component initiating the change.
	 */
	void set_focus(focus_target target, std::string_view source = "unknown");

	// unified app execution and debugging agent APIs
	agentlib::start_app_result start_app(std::string_view args, bool use_debugger, bool auto_continue = true, bool collect_performance = false) override;
	agentlib::start_app_result start_coredump_gdb(std::string_view crash_id) override;
	bool write_to_run(int run_id, std::string_view data) override;
	agentlib::run_screenshot_data get_run_screenshot(int run_id) override;
	int64_t get_run_last_modified_age(int run_id) override;
	void set_run_recording(int run_id, bool recording) override;
	std::vector<std::string> get_run_recorded_data(int run_id) override;
	bool terminate_run(int run_id) override;
	agentlib::wait_for_app_result wait_for_app(int run_id, std::string_view type, int timeout_sec) override;
	ui::terminal_window *find_terminal_window(int run_id);

	// agentlib::document_provider implementation
	std::vector<std::string> get_open_document_paths() const override;
	std::unique_ptr<agentlib::document_snapshot> get_open_document(std::string_view safe_path) const override;
	bool apply_live_edits(std::string_view safe_path, std::string_view edits_json_payload) override;
	void save_all_documents() override;

	void set_status_message(std::string_view message, int priority = 0,
				std::chrono::milliseconds duration = std::chrono::milliseconds::max(),
				std::function<void()> click_handler = nullptr);
	void clear_status_message(int priority);
	std::string get_active_status_message() const;
	void handle_status_bar_click(int mouse_x);
	std::vector<std::shared_ptr<agentlib::ai_agent>> get_all_active_agents() const;

	/**
	 * @brief Routes events from the global queue to the focused component.
	 */
	void dispatch(const editor_event &ev);

      private:
	void new_window(std::string_view filename);
	std::string get_k_block_status_help() const;
	void new_agent_window();
	void new_agent_center_window();
	void new_crashdump_window();
	void new_codereview_window(int focus_item_id = -1);
	void new_diff_window();
	void open_subagent_window(std::shared_ptr<agentlib::ai_agent> subagent);
	void update_window_layout();
	void activate_window(size_t index);
	void update_window_menu();
	void update_recent_files_menu();
	std::shared_ptr<document> get_active_doc() const;
	window *get_active_window() const;

	void dispatch_event_mouse(const editor_event &ev);
	void dispatch_event_ui(const editor_event &ev);
	void dispatch_event_file(const editor_event &ev);
	void dispatch_event_window(const editor_event &ev);
	void dispatch_event_search(const editor_event &ev);
	void dispatch_event_git(const editor_event &ev);
	void dispatch_event_build(const editor_event &ev);
	void dispatch_event_lsp(const editor_event &ev);
	void dispatch_event_key(const editor_event &ev);
	void resolve_dialog(dialog_result res);

	// Executes the given lambda on the MAIN UI thread, blocking the caller until it
	// completes. window_/document_/dialog state must only be touched from the main
	// thread, but several run-control APIs (write_to_run, get_run_screenshot,
	// wait_for_app, ...) are invoked directly from agent background threads when an
	// LLM calls the corresponding tool. Routing these through this helper (which
	// hands the lambda to the main thread via the global event queue and waits on a
	// promise) removes that data race. No-op when already on the main thread.
	template <typename T> T run_on_main_thread(std::function<T()> fn)
	{
		if (is_main_thread()) {
			return fn();
		}
		// Hand the callable to the main UI thread inside a std::packaged_task. Its future
		// is fulfilled when the main thread invokes the task (see dispatch's
		// run_on_main handling), so blocking on fut.get() here is how the background
		// thread waits for the result. Works for both void and non-void return types.
		auto task = std::make_shared<std::packaged_task<T()>>(std::move(fn));
		auto fut = task->get_future();
		editor_event ev;
		ev.type = event_type::run_on_main;
		ev.generic_promise = task;
		global_queue_.push(ev);
		return fut.get();
	}
	void open_file_as_text(std::string_view filename);
	void open_file_as_binary(std::string_view filename);
	void open_prompt_in_editor(ui_multiline_edit *edit, std::string_view initial_text);
	void check_files_changed();
	bool handle_k_block_key(int key);
	bool handle_q_block_key(int key);
	bool handle_p_block_key(int key);
	void handle_inline_agent_prompt_key(int key);
	void launch_inline_agent(std::string_view prompt);
	void execute_vim_command(std::string_view cmd_raw);
	void render(bool cursor_only = false);

	event_queue global_queue_;

	std::thread::id main_thread_id_;
	bool is_main_thread() const noexcept
	{
		return std::this_thread::get_id() == main_thread_id_;
	}

	friend class agentlib::ai_agent;

	menu_bar top_menu_;
	status_bar bottom_status_;

	std::vector<std::shared_ptr<document>> documents_;
	std::vector<std::unique_ptr<window>> windows_;

	focus_target current_focus_{focus_target::window};
	enum class input_mode { normal, k_block, q_block, p_block, searching, search_options, going_to_line, inline_agent, vim, replace_prompt };
	input_mode active_mode_{input_mode::normal};

	std::string vim_input_buffer_;
	bool vim_prefix_mode_{false};

	enum class dialog_mode {
		none,
		load,
		save,
		search,
		replace,
		insert_file,
		settings,
		save_prompt,
		reload_prompt,
		force_quit_prompt,
		ask_user,
		model_list,
		model_edit,
		model_selection,
		model_server_list,
		model_server_edit,
		welcome,
		crash_report,
		run_settings,
		mcp_config,
		mcp_tools,
		a2a_config,
		a2a_server_edit,
		write_block,
		copilot_connect,
		task_models,
		codereview_edit_field,
		codereview_add_comment,
		codereview_select_state,
		codereview_select_severity,
		syntax_colors,
		maybe_binary_prompt,
		insert_file_multiline,
		image_manager,
		image_import_select_file,
		image_save_select_file,
		new_project,
		reopen_ai_settings
	};
	int codereview_edit_item_id_{-1};
	std::string pending_image_vfs_uri_;
	dialog_mode active_dialog_mode_{dialog_mode::none};
	std::unique_ptr<dialog> active_dialog_;
	std::unique_ptr<popup_menu> active_popup_;
	std::shared_ptr<std::promise<std::string>> active_ask_user_promise_;
	std::string configuring_mcp_server_;
	std::string pending_open_filename_;
	ui_multiline_edit *pending_multiline_edit_ptr_{nullptr};
	long long last_clock_minute_{0};

	search_params current_search_;
	std::string search_input_buffer_;

	std::string search_options_buffer_;

	std::string line_input_buffer_;

	std::string inline_agent_input_buffer_;

	struct pending_task {
		std::string prompt;
		bool active{false};
	} pending_inline_agent_task_;

	std::vector<std::shared_ptr<agentlib::ai_agent>> headless_agents_;

	struct status_message {
		std::string text;
		std::chrono::steady_clock::time_point expiry;
		std::function<void()> click_handler;
	};
	std::map<int, status_message> active_status_messages_;
	const status_message *get_active_status_message_obj() const;

	std::string editing_model_id_;
	std::string editing_model_server_id_;
	int switching_agent_id_{-1};
	std::string get_search_autocomplete() const;

	bool is_running_{true};
	bool is_quitting_{false};
	double exit_immediately_{-1.0};
	bool debug_mode_{false};
	std::string debug_string_;
	std::string initial_agent_prompt_;
	bool fresh_agent_{false};
	bool needs_full_redraw_{true};

	bool is_pasting_{false};
	std::string paste_buffer_;

	// Mouse dragging / Window resizing state
	enum class drag_mode { none, move, resize };
	drag_mode current_drag_mode_{drag_mode::none};
	window *drag_window_{nullptr};
	int drag_start_mouse_x_{-1};
	int drag_start_mouse_y_{-1};
	int drag_start_win_x_{-1};
	int drag_start_win_y_{-1};
	int drag_start_win_w_{-1};
	int drag_start_win_h_{-1};

	// Double-click detection state
	int last_click_window_id_{-1};
	bool last_click_on_title_bar_{false};
	std::chrono::steady_clock::time_point last_click_time_;
	std::chrono::steady_clock::time_point last_mtime_check_time_;

	// Double/Triple click detection for content area
	int last_content_click_window_id_{-1};
	int last_content_click_x_{-1};
	int last_content_click_y_{-1};
	std::chrono::steady_clock::time_point last_content_click_time_;
	int content_click_count_{0};

	std::unique_ptr<process_runner> current_build_process_;

	// Latency tracking metrics for user-initiated interactive events
	uint64_t total_latency_us_{0};
	uint64_t max_latency_us_{0};
	uint64_t min_latency_us_{std::numeric_limits<uint64_t>::max()};
	uint64_t total_input_events_{0};
	uint64_t slow_events_count_1ms_{0};
	uint64_t slow_events_count_5ms_{0};
	uint64_t slow_events_count_10ms_{0};
	std::vector<latency_spike> latency_spikes_;
};
