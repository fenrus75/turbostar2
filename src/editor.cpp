#include "editor.h"
#include <algorithm>
#include <chrono>
#include <ncurses.h>
#include "event_logger.h"
#include "file_dialog.h"
#include "find_dialog.h"
#include "history_manager.h"
#include "config_manager.h"
#include "settings_dialog.h"
#include "git_manager.h"
#include "clangd_manager.h"
#include "gcc_log_parser.h"
#include "build_error_manager.h"
#include "fs_utils.h"
#include <fstream>
#include <sstream>
#include <lsp/json/json.h>

namespace fs = std::filesystem;

editor::editor(bool debug_mode, const std::string &debug_string, const std::vector<std::string> &filenames, bool exit_immediately, bool no_lsp)
    : exit_immediately_(exit_immediately), debug_mode_(debug_mode), debug_string_(debug_string)
{
	history_manager::get_instance().load();
	git_manager::get_instance().start(global_queue_);
	bool lsp_allowed = !no_lsp && config_manager::get_instance().is_lsp_enabled();
	if (lsp_allowed) {
		clangd_manager::get_instance().start(global_queue_);
	}

	if (filenames.empty()) {
		new_window("");
	} else {
		for (const auto &f : filenames) {
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

#include "agent_window.h"

void editor::new_agent_window()
{
	auto win = std::make_unique<agent_window>(static_cast<int>(windows_.size() + 1), 0, 1, COLS, LINES - 2, global_queue_, this);
	
	// Add its document to the global documents list so it gets saved on exit/etc if needed (though it shouldn't be saved)
	documents_.push_back(win->get_document());
	
	windows_.push_back(std::move(win));
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

namespace {
class editor_document_snapshot : public agentlib::document_snapshot {
public:
    editor_document_snapshot(std::vector<std::string> lines, std::vector<diagnostic_info> diagnostics) 
        : lines_(std::move(lines)) 
    {
        for (const auto& d : diagnostics) {
            agentlib::diagnostic_snapshot ds;
            ds.line = d.range.start_y;
            ds.column = d.range.start_x;
            ds.message = d.message;
            ds.source = "LSP";
            switch (d.severity) {
                case 1: ds.severity = "Error"; break;
                case 2: ds.severity = "Warning"; break;
                case 3: ds.severity = "Information"; break;
                case 4: ds.severity = "Hint"; break;
                default: ds.severity = "Unknown"; break;
            }
            diagnostics_.push_back(ds);
        }
    }
    
    size_t get_line_count() const override { return lines_.size(); }
    std::string get_line_text(size_t index) const override {
        if (index < lines_.size()) return lines_[index];
        return "";
    }
    std::vector<agentlib::diagnostic_snapshot> get_diagnostics() const override {
        return diagnostics_;
    }
private:
    std::vector<std::string> lines_;
    std::vector<agentlib::diagnostic_snapshot> diagnostics_;
};
} // namespace

std::vector<std::string> editor::get_open_document_paths() const {
    std::vector<std::string> paths;
    for (const auto& doc : documents_) {
        std::string doc_path = doc->get_filename();
        if (doc_path.empty()) continue;
        
        try {
            std::filesystem::path p = std::filesystem::weakly_canonical(doc_path);
            paths.push_back(p.string());
        } catch (...) {
            // Ignore invalid paths
        }
    }
    return paths;
}

std::unique_ptr<agentlib::document_snapshot> editor::get_open_document(const std::string& safe_path) const {
    for (const auto& doc : documents_) {
        std::string doc_path = doc->get_filename();
        if (doc_path.empty()) continue;
        
        try {
            std::filesystem::path p = std::filesystem::weakly_canonical(doc_path);
            if (p.string() == safe_path) {
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
	clangd_manager::get_instance().stop();
	git_manager::get_instance().stop();
}

void editor::run()
{
	render();

	// Set ncurses getch timeout to 50ms to allow background events to
	// process
	timeout(50);
	auto start_time = std::chrono::steady_clock::now();

	while (is_running_) {
		if (exit_immediately_) {
			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 1000) {
				is_running_ = false;
			}
		}

		wint_t wch;
		int res = get_wch(&wch);

		// Handle dialog tick (auto-countdown)
		if (active_dialog_mode_ == dialog_mode::force_quit_prompt) {
			auto s_dialog = dynamic_cast<force_quit_dialog*>(active_dialog_.get());
			if (s_dialog && s_dialog->tick()) {
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
				if (wch == 27) { // ESC sequence
					timeout(0); // Non-blocking for sequence check
					wint_t next_wch;
					int next_res = get_wch(&next_wch);
					if (next_res != ERR && next_res != KEY_CODE_YES && next_wch == '[') {
						wint_t arrow_wch;
						int arrow_res = get_wch(&arrow_wch);
						if (arrow_res != ERR && arrow_res != KEY_CODE_YES) {
							int key = 0;
							switch (arrow_wch) {
								case 'A':
									key = KEY_UP;
									break;
								case 'B':
									key = KEY_DOWN;
									break;
								case 'C':
									key = KEY_RIGHT;
									break;
								case 'D':
									key = KEY_LEFT;
									break;
							}
							if (key != 0) {
								ev.type = event_type::key_press;
								ev.key_code = key;
								global_queue_.push(ev);
							}
						}
					} else if (next_res != ERR) {
						// Alt + key
						ev.type = event_type::key_press;
						ev.key_code = -static_cast<int>(next_wch);
						global_queue_.push(ev);
					} else {
						ev.type = event_type::key_press;
						ev.key_code = 27; // Bare ESC
						global_queue_.push(ev);
					}
					timeout(50); // Restore 50ms timeout
				} else {
					// Printable character or UTF-8 sequence
					ev.type = event_type::key_press;
					ev.key_code = static_cast<int>(wch);

					// Convert wide char to UTF-8 string
					char buf[8];
					int len = wctomb(buf, wch);
					if (len > 0) {
						ev.utf8_char.assign(buf, len);
					}

					global_queue_.push(ev);
				}
			}
		}

		bool needs_render = false;
		while (auto ev = global_queue_.pop()) {
			dispatch(*ev);
			needs_render = true;
		}

		for (auto &w : windows_) {
			if (w->process_events()) {
				needs_render = true;
			}
		}

		if (needs_render) {
			render();
		}
	}

	// Save history on exit
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
		editor_event ev;
		ev.type = event_type::save_all;
		global_queue_.push(ev);
		return true;
	} else if (c == 'r') {
		logger.log("K-block: Insert File");
		active_dialog_ = std::make_unique<file_dialog>("Insert File", file_dialog_mode::open, false, ".");
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
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->format_paragraph();
		}
		return true;
	} else if (c == ']') {
		logger.log("K-block: Expand Selection (LSP)");
		if (active_doc && !active_doc->get_filename().empty()) {
			clangd_manager::get_instance().request_selection_range(
				active_doc->get_filename(),
				active_doc->get_cursor_y(),
				active_doc->get_cursor_x()
			);
		}
		return true;
	} else if (c == '[' || c == '{') {
		logger.log("K-block: Select Scope");
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			active_doc->select_enclosing_scope();
			editor_event redraw_ev;
			redraw_ev.type = event_type::redraw;
			global_queue_.push(redraw_ev);
		}
		return true;
	} else if (c == 'd' || c == 's') {

		logger.log("K-block: Save File");
		editor_event ev;
		ev.type = event_type::save;
		global_queue_.push(ev);
		return true;
	} else if (c == 'w') {
		logger.log("K-block: Write (Save As)");
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
	std::vector<window*> sorted_windows;
	for (auto& w : windows_) {
		sorted_windows.push_back(w.get());
	}

	// Sort: highest priority first. If priority is equal, newest timestamp first.
	std::sort(sorted_windows.begin(), sorted_windows.end(), [active_win](window* a, window* b) {
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
		window* w = sorted_windows[i];
		w->set_visible(true); // Default to visible
		
		// Check if fully covered by any single window above it
		for (size_t j = 0; j < i; ++j) {
			window* above = sorted_windows[j];
			if (above->is_visible() && 
			    w->get_x() >= above->get_x() && 
			    w->get_y() >= above->get_y() &&
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
		status_help = "Q-Block: F:Find A:Replace";
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
	}

	std::string diag_text = "";
	if (active_win && active_win->get_document()) {
		auto doc = active_win->get_document();
		for (const auto& diag : doc->get_lsp_diagnostics()) {
			if (cur_y > diag.range.start_y && cur_y < diag.range.end_y) {
				diag_text = diag.message; break;
			} else if (cur_y == diag.range.start_y && cur_y == diag.range.end_y) {
				if (cur_x >= diag.range.start_x && cur_x <= diag.range.end_x) {
					diag_text = diag.message; break;
				}
			} else if (cur_y == diag.range.start_y && cur_x >= diag.range.start_x) {
				diag_text = diag.message; break;
			} else if (cur_y == diag.range.end_y && cur_x <= diag.range.end_x) {
				diag_text = diag.message; break;
			}
		}
		
		// If no LSP diagnostic, check for GCC build errors
		if (diag_text.empty()) {
			auto build_err = build_error_manager::get_instance().find_error_at(doc->get_filename(), cur_y);
			if (build_err) {
				diag_text = (build_err->is_warning ? "[Warning] " : "[Error] ") + build_err->message;
			}
		}
	}

	std::string combined_hover = hover_text_;
	if (!diag_text.empty()) {
		if (!combined_hover.empty()) combined_hover += " | ";
		combined_hover += diag_text;
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
			curs_set(1);
		}
	}

	refresh();
}

bool editor::apply_live_edits(const std::string& safe_path, const std::string& edits_json_payload) {
    editor_event ev;
    ev.type = event_type::apply_edits;
    ev.payload = safe_path + "\n" + edits_json_payload;
    global_queue_.push(ev);
    return true;
}
