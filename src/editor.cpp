#include "editor.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include <thread>
#include "build_error_manager.h"
#include "config_manager.h"
#include "crashdump_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "git_manager.h"
#include "history_manager.h"
#include "project_manager.h"
#include "ui/dialog_factories.h"
#include "ui/terminal_window.h"

namespace fs = std::filesystem;

editor::editor(editor_options opts)
    : exit_immediately_(opts.exit_immediately), debug_mode_(opts.debug_mode), debug_string_(std::move(opts.debug_string)),
      initial_agent_prompt_(std::move(opts.initial_agent_prompt)), fresh_agent_(opts.fresh_agent)
{
	history_manager::get_instance().load();
	git_manager::get_instance().start(global_queue_);
	bool lsp_allowed = !opts.no_lsp && config_manager::get_instance().is_lsp_enabled();
	if (lsp_allowed) {
		project_manager::get_instance().lsp_start(global_queue_);
	}

	std::string repo_root = git_manager::get_instance().get_repository_root();

	if (opts.filenames.empty()) {
		bool loaded_files = false;
		if (!repo_root.empty()) {
			std::vector<std::string> proj_files = history_manager::get_instance().get_project_files(repo_root);
			if (!proj_files.empty()) {
				for (const auto &f : proj_files) {
					new_window(f);
				}
				loaded_files = true;
			}
		}

		if (!loaded_files) {
			new_window("");
			if (!opts.no_welcome && initial_agent_prompt_.empty()) {
				active_dialog_ = create_welcome_dialog();
				active_dialog_mode_ = dialog_mode::welcome;
				set_focus(focus_target::dialog, "welcome");
			}
		}
	} else {
		for (const auto &f : opts.filenames) {
			new_window(f);
		}
	}
}

void editor::new_window(const std::string &filename)
{
	// Create document
	auto doc = std::make_shared<document>(global_queue_, filename);
	documents_.push_back(doc);

	// Create window
	auto win = std::make_unique<window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, filename);
	win->attach_document(doc);

	windows_.push_back(std::move(win));
	activate_window(windows_.size() - 1);
}

#include "agentlib/ai_model.h"
#include "ui/agent_status_window.h"
#include "ui/agent_window.h"
#include "ui/crashdump_window.h"
#include "ui/diff_window.h"

void editor::new_diff_window()
{
	auto doc = get_active_doc();
	if (!doc)
		return;
	auto diff_win = std::make_unique<diff_window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, doc, global_queue_);
	windows_.push_back(std::move(diff_win));
	update_window_layout();
	activate_window(windows_.size() - 1);
}

void editor::update_window_layout()
{
	bool has_agent = false;
	for (const auto &win : windows_) {
		if (dynamic_cast<agent_window *>(win.get()) != nullptr) {
			has_agent = true;
			break;
		}
	}

	int main_w = COLS;
	int status_w = 0;
	int agent_w = COLS;
	if (has_agent) {
		agent_w = (COLS * 70) / 100;
		status_w = COLS - agent_w;
		main_w = agent_w;
	}

	window *run_win = nullptr;
	window *gdb_win = nullptr;
	for (auto &win : windows_) {
		if (win->get_title() == "Run Output") {
			run_win = win.get();
		}
		if (win->get_title() == "Debugger (GDB)") {
			gdb_win = win.get();
		}
	}

	int total_h = LINES - 2;
	int app_h = (total_h * 2) / 3;
	int gdb_h = total_h - app_h;

	for (auto &win : windows_) {
		if (has_agent && dynamic_cast<agent_status_window *>(win.get())) {
			win->set_bounds(agent_w, 1, status_w, total_h);
		} else if (win.get() == run_win && gdb_win != nullptr) {
			win->set_bounds(0, 1, main_w, app_h);
		} else if (win.get() == gdb_win && run_win != nullptr) {
			win->set_bounds(0, 1 + app_h, main_w, gdb_h);
		} else {
			win->set_bounds(0, 1, main_w, total_h);
		}
		win->invalidate();
	}
}

void editor::new_agent_window()
{
	std::string default_model_id = config_manager::get_instance().get_default_model_id();
	auto model = agentlib::ai_model_registry::get_instance().get_model(default_model_id);
	if (!model) {
		// Fallback if the configured model isn't found
		model = agentlib::ai_model_registry::get_instance().get_model("gpt-4o");
	}

	auto main_agent_win = std::make_unique<agent_window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, model,
							     global_queue_, this, fresh_agent_);

	auto status_win = std::make_unique<agent_status_window>(static_cast<int>(windows_.size() + 2), 0, 1, COLS, LINES - 2,
								"Agent Status", main_agent_win->get_agent(), global_queue_);

	windows_.push_back(std::move(main_agent_win));
	windows_.push_back(std::move(status_win));

	update_window_layout();

	// Activate the main agent window
	activate_window(windows_.size() - 2);
}
void editor::open_subagent_window(std::shared_ptr<agentlib::ai_agent> subagent)
{
	auto subagent_win =
	    std::make_unique<agent_window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, std::move(subagent));

	windows_.push_back(std::move(subagent_win));
	update_window_layout();

	// Activate the new subagent window (it will be resized by update_window_layout)
	activate_window(windows_.size() - 1);
}

void editor::new_crashdump_window()
{
	auto dump_win = std::make_unique<crashdump_window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2);
	windows_.push_back(std::move(dump_win));
	update_window_layout();
	activate_window(windows_.size() - 1);
}

void editor::activate_window(size_t index)
{
	if (index >= windows_.size())
		return;

	for (size_t i = 0; i < windows_.size(); ++i) {
		windows_[i]->set_active(i == index);
	}
	update_window_menu();
}

void editor::update_window_menu()
{
	std::vector<menu_item> items;

	items.push_back(menu_item("Close", event_type::close_window, 0, 'c', "Alt+F3", false));
	items.push_back(menu_item("", event_type::key_press, 0, 0, "", true)); // Separator

	for (size_t i = 0; i < windows_.size(); ++i) {
		std::string filename = windows_[i]->get_title();
		if (filename.empty())
			filename = "noname.txt";

		auto doc = windows_[i]->get_document();
		if (doc && doc->is_modified()) {
			filename += "*";
		}

		std::string name = std::to_string((i + 1) % 10) + " " + filename;

		std::string shortcut = "";
		char hotkey = 0;
		if (i < 10) {
			int num = static_cast<int>((i + 1) % 10);
			shortcut = "Alt-" + std::to_string(num);
			hotkey = static_cast<char>('0' + num);
		}

		items.push_back(menu_item(name, event_type::select_window, static_cast<int>(i), hotkey, shortcut, false));
	}
	event_logger::get_instance().log("update_window_menu: " + std::to_string(items.size()) + " items");
	top_menu_.set_category_items("Window", items);

	auto active_doc = get_active_doc();
	bool read_only = (!active_doc || active_doc->is_read_only());
	top_menu_.set_item_disabled(event_type::save, read_only);
	top_menu_.set_item_disabled(event_type::save_as, read_only);
	top_menu_.set_item_disabled(event_type::save_all, read_only);
	top_menu_.set_item_disabled(event_type::run_program, config_manager::get_instance().get_main_executable().empty());
}

std::shared_ptr<document> editor::get_active_doc() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w->get_document();
		}
	}
	if (!documents_.empty()) {
		return documents_[0];
	}
	return nullptr;
}

window *editor::get_active_window() const
{
	for (auto &w : windows_) {
		if (w->is_active()) {
			return w.get();
		}
	}
	return nullptr;
}

std::string editor::get_search_autocomplete() const
{
	if (search_input_buffer_.empty())
		return "";

	const auto &history = history_manager::get_instance().get_searches();
	for (const auto &item : history) {
		if (item.length() >= search_input_buffer_.length() &&
		    item.substr(0, search_input_buffer_.length()) == search_input_buffer_) {
			return item;
		}
	}
	return "";
}

namespace
{
class editor_document_snapshot : public agentlib::document_snapshot
{
      public:
	editor_document_snapshot(std::vector<std::string> lines, std::vector<diagnostic_info> diagnostics) : lines_(std::move(lines))
	{
		for (const auto &d : diagnostics) {
			agentlib::diagnostic_snapshot ds;
			ds.line = d.range.start_y;
			ds.column = d.range.start_x;
			ds.message = d.message;
			ds.source = "LSP";
			switch (d.severity) {
				case 1:
					ds.severity = "Error";
					break;
				case 2:
					ds.severity = "Warning";
					break;
				case 3:
					ds.severity = "Information";
					break;
				case 4:
					ds.severity = "Hint";
					break;
				default:
					ds.severity = "Unknown";
					break;
			}
			diagnostics_.push_back(ds);
		}
	}

	size_t get_line_count() const override
	{
		return lines_.size();
	}
	std::string get_line_text(size_t index) const override
	{
		if (index < lines_.size())
			return lines_[index];
		return "";
	}
	std::vector<agentlib::diagnostic_snapshot> get_diagnostics() const override
	{
		return diagnostics_;
	}

      private:
	std::vector<std::string> lines_;
	std::vector<agentlib::diagnostic_snapshot> diagnostics_;
};
} // namespace

std::vector<std::string> editor::get_open_document_paths() const
{
	std::vector<std::string> paths;
	for (const auto &doc : documents_) {
		std::string doc_path = doc->get_filename();
		if (doc_path.empty())
			continue;

		try {
			std::filesystem::path p = std::filesystem::weakly_canonical(doc_path);
			paths.push_back(p.string());
		} catch (...) {
			// Ignore invalid paths
		}
	}
	return paths;
}

std::unique_ptr<agentlib::document_snapshot> editor::get_open_document(const std::string &safe_path) const
{
	std::filesystem::path target_p(safe_path);
	for (const auto &doc : documents_) {
		std::string doc_path = doc->get_filename();
		if (doc_path.empty())
			continue;

		try {
			std::filesystem::path p(doc_path);
			if (std::filesystem::exists(p) && std::filesystem::exists(target_p)) {
				if (std::filesystem::equivalent(p, target_p)) {
					return std::make_unique<editor_document_snapshot>(doc->get_all_lines(), doc->get_diagnostics());
				}
			}

			// Fallback for non-existent files or if equivalent fails (e.g. relative vs absolute)
			if (std::filesystem::weakly_canonical(p).string() == std::filesystem::weakly_canonical(target_p).string()) {
				return std::make_unique<editor_document_snapshot>(doc->get_all_lines(), doc->get_diagnostics());
			}
		} catch (...) {
			// Ignore invalid paths
		}
	}
	return nullptr;
}

editor::~editor()
{
	project_manager::get_instance().lsp_stop();
	git_manager::get_instance().stop();
}

void editor::run()
{
	if (!initial_agent_prompt_.empty()) {
		new_agent_window();
		// Look for the agent window we just created to send the message
		for (auto &win : windows_) {
			if (auto agent_win = dynamic_cast<agent_window *>(win.get())) {
				if (auto agent = agent_win->get_agent()) {
					agent->submit_prompt(initial_agent_prompt_);
				}
				break;
			}
		}
		// Clear it so we don't send it again
		initial_agent_prompt_.clear();
	}

	render();

	// Set ncurses getch timeout to 50ms to allow background events to
	// process
	timeout(50);
	auto start_time = std::chrono::steady_clock::now();

	while (is_running_) {
		if (exit_immediately_ >= 0.0) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() >
			    static_cast<long long>(exit_immediately_ * 1000.0)) {
				event_logger::get_instance().log("Application exit requested (source: --exit-immediately timer).");
				is_running_ = false;
			}
		}

		wint_t wch;
		int res = get_wch(&wch);

		// Handle dialog tick (auto-countdown)
		if (active_dialog_mode_ == dialog_mode::force_quit_prompt && active_dialog_) {
			if (active_dialog_->tick()) {
				// Countdown expired, force quit
				editor_event quit_ev;
				quit_ev.type = event_type::force_quit;
				global_queue_.push(quit_ev);
			} else {
				// Re-render to show updated countdown
				render();
			}
		}

		if (res != ERR) {
			editor_event ev;
			if (res == KEY_CODE_YES) {
				if (wch == KEY_MOUSE) {
					MEVENT event;
					if (getmouse(&event) == OK) {
						if (event.bstate & BUTTON1_PRESSED || event.bstate & BUTTON1_CLICKED) {
							ev.type = event_type::mouse_click;
							ev.mouse_x = event.x;
							ev.mouse_y = event.y;
							global_queue_.push(ev);
						} else if (event.bstate & BUTTON4_PRESSED) {
							ev.type = event_type::mouse_scroll_up;
							ev.mouse_x = event.x;
							ev.mouse_y = event.y;
							global_queue_.push(ev);
						} else if (event.bstate & BUTTON5_PRESSED) {
							ev.type = event_type::mouse_scroll_down;
							ev.mouse_x = event.x;
							ev.mouse_y = event.y;
							global_queue_.push(ev);
						}
					}
				} else {
					// Functional keys
					ev.type = event_type::key_press;
					ev.key_code = static_cast<int>(wch);
					global_queue_.push(ev);
				}
			} else {
				// Character or ESC sequence
				if (wch == 27) {    // ESC sequence
					timeout(0); // Non-blocking for sequence check
					wint_t next_wch;
					int next_res = get_wch(&next_wch);
					if (next_res != ERR && next_res != KEY_CODE_YES && next_wch == '[') {
						std::string seq;
						wint_t seq_wch;
						while (get_wch(&seq_wch) != ERR) {
							seq += (char)seq_wch;
							if (seq_wch == '~' || seq.length() > 10)
								break;
						}

						if (seq == "200~") {
							if (is_pasting_ && !paste_buffer_.empty()) {
								ev.type = event_type::paste;
								ev.payload = paste_buffer_;
								global_queue_.push(ev);
							}
							is_pasting_ = true;
							paste_buffer_.clear();
						} else if (seq == "201~") {
							is_pasting_ = false;
							if (!paste_buffer_.empty()) {
								ev.type = event_type::paste;
								ev.payload = paste_buffer_;
								global_queue_.push(ev);
								paste_buffer_.clear();
							}
						} else {
							if (is_pasting_) {
								paste_buffer_ += "\033[";
								paste_buffer_ += seq;
							} else {
								int key = 0;
								if (seq == "A")
									key = KEY_UP;
								else if (seq == "B")
									key = KEY_DOWN;
								else if (seq == "C")
									key = KEY_RIGHT;
								else if (seq == "D")
									key = KEY_LEFT;

								if (key != 0) {
									ev.type = event_type::key_press;
									ev.key_code = key;
									global_queue_.push(ev);
								}
							}
						}
					} else if (next_res != ERR) {
						// Alt + key
						if (is_pasting_) {
							paste_buffer_ += (char)27;
							char buf[8];
							int len = wctomb(buf, next_wch);
							if (len > 0)
								paste_buffer_.append(buf, len);
						} else {
							ev.type = event_type::key_press;
							ev.key_code = -static_cast<int>(next_wch);
							global_queue_.push(ev);
						}
					} else {
						if (is_pasting_) {
							paste_buffer_ += (char)27;
						} else {
							ev.type = event_type::key_press;
							ev.key_code = 27; // Bare ESC
							global_queue_.push(ev);
						}
					}
					timeout(50); // Restore 50ms timeout
				} else {
					// Printable character or UTF-8 sequence
					// Convert wide char to UTF-8 string
					char buf[8];
					int len = wctomb(buf, wch);
					if (len > 0) {
						if (is_pasting_) {
							paste_buffer_.append(buf, len);
						} else {
							ev.type = event_type::key_press;
							ev.key_code = static_cast<int>(wch);
							ev.utf8_char.assign(buf, len);
							global_queue_.push(ev);
						}
					}
				}
			}
		}

		bool needs_render = false;
		while (auto ev = global_queue_.pop()) {
			dispatch(*ev);
			needs_render = true;
		}

		bool debug_exited = false;
		for (auto &w : windows_) {
			if (auto tw = dynamic_cast<ui::terminal_window *>(w.get())) {
				if (tw->update_pty()) {
					needs_render = true;
				}
				if (tw->get_title() == "Debugger (GDB)" && !tw->is_alive()) {
					debug_exited = true;
				}
			}
			if (w->process_events()) {
				needs_render = true;
			}
		}

		if (debug_exited) {
			for (auto it = windows_.begin(); it != windows_.end();) {
				if ((*it)->get_title() == "Run Output" || (*it)->get_title() == "Debugger (GDB)") {
					if (auto tw = dynamic_cast<ui::terminal_window *>(it->get())) {
						tw->stop_process();
					}
					it = windows_.erase(it);
					needs_render = true;
				} else {
					++it;
				}
			}
			update_window_layout();
			if (!windows_.empty()) {
				activate_window(0);
				set_focus(focus_target::window, "debugger_exit");
			} else {
				set_focus(focus_target::menu_bar, "debugger_exit");
			}
		}

		if (needs_render) {
			render();
		}
	}

	// Save cursor and project history on exit
	for (const auto &doc : documents_) {
		if (doc && !doc->get_filename().empty() && doc->has_nondefault_filename() && !doc->is_read_only()) {
			history_manager::get_instance().set_cursor_pos(doc->get_filename(), doc->get_cursor_x(), doc->get_cursor_y());
		}
	}

	std::string repo_root = git_manager::get_instance().get_repository_root();
	if (!repo_root.empty()) {
		std::vector<std::string> open_files;
		for (const auto &w : windows_) {
			auto doc = w->get_document();
			if (doc && !doc->get_filename().empty() && doc->has_nondefault_filename() && !doc->is_read_only()) {
				open_files.push_back(doc->get_filename());
			}
		}
		history_manager::get_instance().set_project_files(repo_root, open_files);
	}

	history_manager::get_instance().save();
}

void editor::set_focus(focus_target target, const std::string &source)
{
	std::string target_name;
	switch (target) {
		case focus_target::menu_bar:
			target_name = "menu_bar";
			break;
		case focus_target::window:
			target_name = "window";
			break;
		case focus_target::dialog:
			target_name = "dialog";
			break;
		case focus_target::popup:
			target_name = "popup";
			break;
	}

	event_logger::get_instance().log("Focus change: " + source + " -> " + target_name);
	current_focus_ = target;

	if (target == focus_target::menu_bar) {
		update_window_menu();
	}
}

bool editor::handle_k_block_key(int key)
{
	auto &logger = event_logger::get_instance();
	logger.log("K-block handling key: " + std::to_string(key));

	char c = 0;
	if (key > 0 && key <= 26) {
		c = static_cast<char>(key + 'a' - 1);
	} else if (key >= 0 && key < 256) {
		c = std::tolower(static_cast<char>(key));
	}

	if (c == 0)
		return false;

	// Find active window/doc
	std::shared_ptr<document> active_doc = get_active_doc();

	if (!active_doc)
		return false;

	if (c == 'b') {
		logger.log("K-block: Set Selection Begin");
		active_doc->set_selection_start();
		return true;
	} else if (c == 'k') {
		logger.log("K-block: Set Selection End");
		active_doc->set_selection_end();
		return true;
	} else if (c == 'h') {
		logger.log("K-block: Clear Selection");
		active_doc->clear_selection();
		return true;
	} else if (c == 'y') {
		logger.log("K-block: Delete Block");
		active_doc->delete_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'c') {
		logger.log("K-block: Copy Block");
		active_doc->copy_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'm') {
		logger.log("K-block: Move Block");
		active_doc->move_selection();
		editor_event redraw_ev;
		redraw_ev.type = event_type::redraw;
		global_queue_.push(redraw_ev);
		return true;
	} else if (c == 'n') {
		logger.log("K-block: New Window");
		new_window("");
		return true;
	} else if (c == 'p') {
		logger.log("K-block: Compile File");
		editor_event ev;
		ev.type = event_type::compile_file;
		global_queue_.push(ev);
		return true;
	} else if (c == 'g') {
		logger.log("K-block: Next Error");
		editor_event err_ev;
		err_ev.type = event_type::next_error;
		global_queue_.push(err_ev);
		return true;
	} else if (c == 'e') {
		logger.log("K-block: Edit (Open File)");
		editor_event ev;
		ev.type = event_type::load;
		global_queue_.push(ev);
		return true;
	} else if (c == 'a') {
		logger.log("K-block: Save All");
		if (active_doc && active_doc->is_read_only()) {
			logger.log("Cannot save-all read-only buffer via hotkey.");
			return true;
		}
		editor_event ev;
		ev.type = event_type::save_all;
		global_queue_.push(ev);
		return true;
	} else if (c == 'r') {
		logger.log("K-block: Insert File");
		active_dialog_ = create_file_dialog("Insert File", ".");
		active_dialog_mode_ = dialog_mode::insert_file;
		set_focus(focus_target::dialog, "menu_insert_file");
		return true;
	} else if (c == 't') {
		logger.log("K-block: Revert");
		editor_event ev;
		ev.type = event_type::revert;
		global_queue_.push(ev);
		return true;
	} else if (c == 'j') {
		logger.log("K-block: Format Paragraph");
		if (active_doc) {
			active_doc->format_paragraph();
		}
		return true;
	} else if (c == ']') {
		logger.log("K-block: Expand Selection (LSP)");
		if (active_doc && !active_doc->get_filename().empty()) {
			project_manager::get_instance().lsp_request_selection_range(active_doc->get_filename(), active_doc->get_cursor_y(),
										    active_doc->get_cursor_x());
		}
		return true;
	} else if (c == '[' || c == '{') {
		logger.log("K-block: Select Scope");
		if (active_doc) {
			active_doc->select_enclosing_scope();
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return true;
	} else if (c == 'd' || c == 's') {
		logger.log("K-block: Save File");
		if (!active_doc || active_doc->is_read_only()) {
			logger.log("Cannot save read-only or empty buffer via hotkey.");
			return true;
		}
		editor_event ev;
		ev.type = event_type::save;
		global_queue_.push(ev);
		return true;
	} else if (c == 'w') {
		logger.log("K-block: Write (Save As)");
		if (!active_doc || active_doc->is_read_only()) {
			logger.log("Cannot save-as read-only or empty buffer via hotkey.");
			return true;
		}
		editor_event ev;
		ev.type = event_type::save_as;
		global_queue_.push(ev);
		return true;
	} else if (c == 'q') {
		logger.log("K-block: Quit (Abort)");
		editor_event quit_ev;
		quit_ev.type = event_type::quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'x') {
		logger.log("K-block: Force Exit");
		editor_event quit_ev;
		quit_ev.type = event_type::force_quit;
		global_queue_.push(quit_ev);
		return true;
	} else if (c == 'f') {
		logger.log("K-block: Find (Status Bar Prompt)");
		is_searching_prompt_ = true;
		search_input_buffer_ = "";
		return true;
	} else if (c == 'l') {
		logger.log("K-block: Go to Line (Status Bar Prompt)");
		is_going_to_line_prompt_ = true;
		line_input_buffer_ = "";
		return true;
	} else if (c == 'u') {
		logger.log("K-block: Top of File");
		active_doc->move_to_top();
		return true;
	} else if (c == 'v') {
		logger.log("K-block: End of File");
		active_doc->move_to_bottom();
		return true;
	}

	return false;
}

void editor::render()
{
	curs_set(0); // Default to hidden

	// Paint desktop background with dithered pattern
	attron(COLOR_PAIR(9));
	for (int y = 1; y < LINES - 1; ++y) {
		move(y, 0);
		for (int x = 0; x < COLS; ++x) {
			addstr("▒");
		}
	}
	attroff(COLOR_PAIR(9));

	// 2. Windows (Z-Index Managed)
	window *active_win = get_active_window();

	// Create a list of raw pointers to sort
	std::vector<window *> sorted_windows;
	for (auto &w : windows_) {
		sorted_windows.push_back(w.get());
	}

	// Sort: highest priority first. If priority is equal, newest timestamp first.
	std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window *a, window *b) {
		int priority_a = (a == active_win) ? 9999 : a->get_display_priority();
		int priority_b = (b == active_win) ? 9999 : b->get_display_priority();

		if (priority_a != priority_b) {
			return priority_a > priority_b;
		}
		return a->get_last_active_timestamp() > b->get_last_active_timestamp();
	});

	// Occlusion culling: Assume visible, hide if fully covered by earlier (higher priority) windows.
	// For simplicity in this text editor (and since windows are mostly rectangles),
	// a window is fully occluded if it's completely inside another single window above it.
	for (size_t i = 0; i < sorted_windows.size(); ++i) {
		window *w = sorted_windows[i];
		w->set_visible(true); // Default to visible

		// Check if fully covered by any single window above it
		for (size_t j = 0; j < i; ++j) {
			window *above = sorted_windows[j];
			if (above->is_visible() && w->get_x() >= above->get_x() && w->get_y() >= above->get_y() &&
			    w->get_x() + w->get_width() <= above->get_x() + above->get_width() &&
			    w->get_y() + w->get_height() <= above->get_y() + above->get_height()) {
				w->set_visible(false);
				break;
			}
		}
	}

	// Draw backwards (lowest priority first, so highest priority is on top)
	for (auto it = sorted_windows.rbegin(); it != sorted_windows.rend(); ++it) {
		if ((*it)->is_visible()) {
			(*it)->draw();
		}
	}

	top_menu_.draw();

	std::string debug_out;
	int cur_x = -1, cur_y = -1;

	if (debug_mode_) {
		auto &logger = event_logger::get_instance();
		auto msg = logger.get_latest_matching_message(debug_string_);
		if (msg) {
			debug_out = ">>" + *msg + "<<";
		}
	}

	if (active_win) {
		cur_x = active_win->get_cursor_x();
		cur_y = active_win->get_cursor_y();
	}

	std::string status_help = debug_out;
	if (k_block_mode_) {
		status_help = "K-Block: B:Beg K:End Y:Del C:Copy M:Move U:Top "
			      "V:End Q:Quit X:SaveExit F:Find";
	} else if (q_block_mode_) {
		status_help = "Q-Block: F:Find A:Replace H:History";
	} else if (p_block_mode_) {
		status_help = "Agent: (R)eformat (F)ix Warn (C)omment Re(v)iew (T)ODOs (S)pell (U)ser: _";
	} else if (is_inline_agent_prompt_) {
		status_help = "Agent Task: " + inline_agent_input_buffer_ + "_";
	} else if (is_searching_prompt_) {
		std::string suggestion = get_search_autocomplete();
		if (!suggestion.empty() && suggestion != search_input_buffer_) {
			status_help = "Search for: " + search_input_buffer_ + "[" + suggestion.substr(search_input_buffer_.length()) + "]";
		} else {
			status_help = "Search for: " + search_input_buffer_ + "_";
		}
	} else if (is_search_options_prompt_) {
		status_help = "Options (I R B K): " + search_options_buffer_ + "_";
	} else if (is_going_to_line_prompt_) {
		status_help = "Go to line: " + line_input_buffer_ + "_";
	} else if (is_vim_prompt_) {
		status_help = ":" + vim_input_buffer_ + "_";
	}

	std::string diag_text = "";
	if (active_win && active_win->get_document()) {
		auto doc = active_win->get_document();
		for (const auto &diag : doc->get_lsp_diagnostics()) {
			if (cur_y > diag.range.start_y && cur_y < diag.range.end_y) {
				diag_text = diag.message;
				break;
			} else if (cur_y == diag.range.start_y && cur_y == diag.range.end_y) {
				if (cur_x >= diag.range.start_x && cur_x <= diag.range.end_x) {
					diag_text = diag.message;
					break;
				}
			} else if (cur_y == diag.range.start_y && cur_x >= diag.range.start_x) {
				diag_text = diag.message;
				break;
			} else if (cur_y == diag.range.end_y && cur_x <= diag.range.end_x) {
				diag_text = diag.message;
				break;
			}
		}

		// If no LSP diagnostic, check for GCC build errors
		if (diag_text.empty()) {
			auto build_err = build_error_manager::get_instance().find_error_at(doc->get_safe_filename(), cur_y);
			if (build_err) {
				diag_text = (build_err->is_warning ? "[Warning] " : "[Error] ") + build_err->message;
			}
		}
	}

	std::string combined_hover = hover_text_;
	if (!diag_text.empty()) {
		if (!combined_hover.empty())
			combined_hover += " | ";
		combined_hover += diag_text;
	}

	// Find active agent status
	std::string agent_status_text = "";
	for (const auto &win : windows_) {
		if (auto aw = dynamic_cast<agent_window *>(win.get())) {
			auto agent = aw->get_agent();
			if (agent && agent->get_status() != agentlib::agent_status::idle) {
				agent_status_text =
				    "Agent: " + agentlib::agent_status_to_string(agent->get_status(), agent->get_current_tool());
				break;
			}
		}
	}

	if (!agent_status_text.empty()) {
		if (!combined_hover.empty())
			combined_hover += " | ";
		combined_hover += agent_status_text;
	}

	// Check for transient status message
	if (!transient_status_message_.empty()) {
		if (std::chrono::steady_clock::now() < transient_status_expiry_) {
			if (!combined_hover.empty())
				combined_hover += " | ";
			combined_hover += "[ " + transient_status_message_ + " ]";
		} else {
			transient_status_message_ = "";
		}
	}

	bottom_status_.draw(status_help, combined_hover, cur_x, cur_y);

	if (active_dialog_) {
		active_dialog_->draw();
	}

	if (active_popup_) {
		active_popup_->draw();
	}

	// Only show cursor if we are in window focus and NOT in a modal state
	if (current_focus_ == focus_target::window && !active_dialog_ && !active_popup_ && !k_block_mode_) {
		if (active_win) {
			active_win->set_cursor_position();
			if (active_win->is_cursor_visible()) {
				curs_set(1);
			} else {
				curs_set(0);
			}
		}
	}

	refresh();
}

bool editor::apply_live_edits(const std::string &safe_path, const std::string &edits_json_payload)
{
	editor_event ev;
	ev.type = event_type::apply_edits;
	ev.payload = safe_path + "\n" + edits_json_payload;
	global_queue_.push(ev);
	return true;
}

void editor::save_all_documents()
{
	for (auto &doc : documents_) {
		if (doc && doc->is_modified() && !doc->get_filename().empty()) {
			doc->save();
			history_manager::get_instance().add_file(doc->get_filename());
			for (auto &w : windows_) {
				if (w->get_document() == doc) {
					w->set_title(doc->get_filename());
					break;
				}
			}
		}
	}
	update_window_menu();
}
