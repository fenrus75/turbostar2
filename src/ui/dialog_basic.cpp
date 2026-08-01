/* IMPORTANT:
 * Standard pattern for dialog boxes is to set their size automatically after a ->flow call:
 *	dlg->flow();
 *	dlg->set_width(flow_ptr->width());
 *	dlg->set_height(flow_ptr->height());
 */

#include "ui/dialog_factories.h"
#include "ansi.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "ncurses.h"
#include "utf8.h"

#include "agentlib/agent_animation.h"

#include "ui/components/ui_ask_user_group.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_durmovie.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_multiline_edit.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

std::unique_ptr<dialog> create_save_prompt_dialog(const std::string &filename)
{
	int max_dlg_width = COLS > 8 ? COLS - 8 : 50;
	if (max_dlg_width > 120)
		max_dlg_width = 120; // reasonable upper bound

	int msg_overhead = 17; // "Save changes to " + "?"

	std::string display_name = filename;
	if (static_cast<int>(display_name.length()) + msg_overhead + 4 > max_dlg_width) {
		int max_filename_len = max_dlg_width - msg_overhead - 4;
		if (max_filename_len < 10)
			max_filename_len = 10;
		display_name = fs_utils::shorten_filename(display_name, max_filename_len);
	}

	int desired_width = std::max(50, static_cast<int>(display_name.length()) + msg_overhead + 4);

	auto dlg = std::make_unique<dialog>("Unsaved Changes", desired_width, 8);

	auto flow = std::make_unique<ui_vertical_flow>("save_prompt_flow", 2, 2);

	std::string msg = std::format("Save changes to {}?", display_name);
	auto label = std::make_unique<ui_text_label>(msg, true);
	label->set_width(desired_width - 4);
	flow->add_child(std::move(label));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_save", "Save", 'S', [d = dlg.get()]() {
		d->set_result("save");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>("btn_discard", "Discard", 'D', [d = dlg.get()]() {
		d->set_result("discard");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_result("cancel");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("btn_save");

	return dlg;
}

std::unique_ptr<dialog> create_input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value)
{
	auto dlg = std::make_unique<dialog>(title, 60, 10);

	auto flow = std::make_unique<ui_vertical_flow>("input_flow", 2, 1);

	auto label = std::make_unique<ui_text_label>(prompt);
	label->set_width(56);
	flow->add_child(std::move(label));

	auto tb = std::make_unique<ui_textbox>("input_textbox", 56, initial_value);
	flow->add_child(std::move(tb));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'o', [d = dlg.get()]() {
		auto val = d->get_value("input_textbox");
		d->set_result(val ? *val : "");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'c',
	    [d = dlg.get()]() {
		    d->set_result("");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("input_textbox");
	return dlg;
}

std::unique_ptr<dialog> create_message_dialog(const std::string &title, const std::vector<std::string> &lines, int spacer)
{
	int width = 40;
	for (const auto &line : lines) {
		if (static_cast<int>(line.length()) + 6 > width) {
			width = static_cast<int>(line.length()) + 6;
		}
	}
	auto dlg = std::make_unique<dialog>(title, width, 10);

	auto flow = std::make_unique<ui_vertical_flow>("message_flow", 3, 2, spacer);

	for (const auto &line : lines) {
		auto label = std::make_unique<ui_text_label>(line, true);
		label->set_width(width - 6);
		flow->add_child(std::move(label));
	}

	std::string ok_text = "OK";
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(std::max(width, flow_ptr->width()));
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("btn_ok");
	return dlg;
}

class welcome_dialog_impl : public dialog
{
      public:
	welcome_dialog_impl(const std::string &title, int width, int height) : dialog(title, width, height)
	{
	}

	dialog_result handle_key(int key) override
	{
		// Cancel on any keypress to dismiss the welcome screen
		if (key != KEY_MOUSE) {
			action_ = dialog_result::cancelled;
		}
		return action_;
	}

	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override
	{
		if (ev.type == event_type::key_press) {
			action_ = dialog_result::cancelled;
			return true;
		} else if (ev.type == event_type::mouse_click) {
			action_ = dialog_result::cancelled;
			return true;
		}
		return dialog::handle_event(ev, abs_x, abs_y);
	}

	bool tick() override
	{
		update_animation();
		return false;
	}
};

std::unique_ptr<dialog> create_welcome_dialog()
{
	std::vector<std::string> lines = {
		std::format("Version {}", TURBOSTAR_VERSION), 
		""

	};

	int flow_width = 38;
	int width = flow_width + 6;

	auto dlg = std::make_unique<welcome_dialog_impl>("About Turbostar", width, 22);

	auto flow = std::make_unique<ui_vertical_flow>("welcome_flow", 3, 1, 0);

	auto movie = std::make_unique<ui_durmovie>("turbostar_movie", 0, 0, 35, 16);
	auto &reg = agentlib::agent_animation_registry::get_instance();
	auto anim = reg.get_animation("turbostar");
	movie->set_animation(anim);
	movie->set_state(durmovie_state::active);
	flow->add_child(std::move(movie));

	for (const auto &line : lines) {
		auto label = std::make_unique<ui_text_label>(line, true);
		label->set_width(flow_width);
		flow->add_child(std::move(label));
	}

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_new_project", "New Project...", 'n', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("new_project");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("ok");
	}));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("btn_ok");
	return dlg;
}

std::unique_ptr<dialog> create_crash_dialog(const std::string &crash_text, const std::string &crash_file_path)
{
	int width = 66;
	int height = 15;

	auto dlg = std::make_unique<dialog>("Oops, you did something we did not think of", width, height);
	auto flow = std::make_unique<ui_vertical_flow>("crash_flow", 2, 2);

	flow->add_child(std::make_unique<ui_text_label>("Turbostar crashed in a previous run.", true));
	flow->add_child(std::make_unique<ui_text_label>("Please report this issue on GitHub to help us fix it.", true));
	flow->add_child(std::make_unique<ui_text_label>("", true)); // Spacer

	// Parse crash info for display
	std::string signal_line = "";
	std::string frame0 = "";
	std::string frame1 = "";
	std::stringstream ss(crash_text);
	std::string line;
	while (std::getline(ss, line)) {
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
			line.pop_back();
		}
		if (line.find("Caught signal:") != std::string::npos) {
			signal_line = line;
			size_t idx = signal_line.find("Caught signal:");
			signal_line = signal_line.substr(idx);
		} else if (line.find("  #0 ") != std::string::npos) {
			size_t idx = line.find("  #0 ");
			frame0 = line.substr(idx);
		} else if (line.find("  #1 ") != std::string::npos) {
			size_t idx = line.find("  #1 ");
			frame1 = line.substr(idx);
		}
	}

	if (!signal_line.empty()) {
		flow->add_child(std::make_unique<ui_text_label>(signal_line, true));
	}
	if (!frame0.empty()) {
		flow->add_child(std::make_unique<ui_text_label>(frame0, true));
	}
	if (!frame1.empty()) {
		flow->add_child(std::make_unique<ui_text_label>(frame1, true));
	}
	flow->add_child(std::make_unique<ui_text_label>("", true)); // Spacer

	// Buttons
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);

	// 1. Copy button
	btns->add_child(std::make_unique<ui_button>("btn_copy", "Copy Stack Trace", 'C', [d = dlg.get(), crash_text]() {
		ansi::copy_to_clipboard(crash_text);
		d->set_action(dialog_result::confirmed);
	}));

	// 2. Report on GitHub button (if /usr/bin/gh is present)
	if (std::filesystem::exists("/usr/bin/gh")) {
		btns->add_child(std::make_unique<ui_button>("btn_gh", "Report on GitHub", 'R', [d = dlg.get(), crash_text, signal_line]() {
			std::string title = signal_line.empty() ? "Turbostar Crash Report" : signal_line;
			for (char &c : title) {
				if (c == '"' || c == '\'' || c == '\\' || c == '`') c = ' ';
			}
			std::string temp_body_path = "/tmp/turbostar_crash_body.txt";
			std::ofstream out(temp_body_path);
			if (out.is_open()) {
				out << crash_text;
				out.close();
				std::string cmd = std::format("/usr/bin/gh issue create --title \"{}\" --body-file \"{}\" >/dev/null 2>&1", title, temp_body_path);
				int ret = std::system(cmd.c_str());
				(void)ret;
				std::filesystem::remove(temp_body_path);
			}
			d->set_action(dialog_result::confirmed);
		}));
	}

	// 3. Ignore button
	btns->add_child(std::make_unique<ui_button>("btn_ignore", "Ignore", 'I', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
	}, true)); // press_on_esc = true

	auto flow_ptr = flow.get();
	flow->add_child(std::move(btns));
	dlg->add_child(std::move(flow));

	// Move the file to crashes.old/ so we don't ask again
	namespace fs = std::filesystem;
	try {
		fs::path src_path(crash_file_path);
		fs::path cache_dir = fs::path(fs_utils::get_global_cache_dir());
		fs::path old_dir = cache_dir / "crashes.old";
		fs::create_directories(old_dir);
		fs::path dst_path = old_dir / src_path.filename();
		std::error_code ec;
		fs::rename(src_path, dst_path, ec);
		if (ec) {
			fs::copy_file(src_path, dst_path, fs::copy_options::overwrite_existing, ec);
			fs::remove(src_path, ec);
		}
	} catch (...) {}

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());
	dlg->set_focus_by_name("btn_ignore");
	return dlg;
}

class force_quit_dialog_impl : public dialog
{
      public:
	force_quit_dialog_impl() : dialog("Force Quit", 50, 9)
	{
		start_time_ = std::chrono::steady_clock::now();

		auto flow = std::make_unique<ui_vertical_flow>("force_quit_flow", 2, 2);

		std::string msg = "Unsaved changes! Quit anyway?";
		auto msg_label = std::make_unique<ui_text_label>(msg, true);
		msg_label->set_width(width_ - 4);
		flow->add_child(std::move(msg_label));

		auto count_label = std::make_unique<ui_text_label>("(Auto-closing in 5s)", true);
		count_label->set_width(width_ - 4);
		countdown_label_ = count_label.get();
		flow->add_child(std::move(count_label));

		auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
		btns->set_centered(true);
		btns->add_child(std::make_unique<ui_button>("btn_exit", "Exit", 'E', [this]() {
			set_action(dialog_result::confirmed);
			set_result("exit");
		}));
		btns->add_child(std::make_unique<ui_button>("btn_save_all", "Save All", 'S', [this]() {
			set_action(dialog_result::confirmed);
			set_result("save_all");
		}));
		btns->add_child(std::make_unique<ui_button>(
		    "btn_cancel", "Cancel", 'C',
		    [this]() {
			    set_action(dialog_result::cancelled);
			    set_result("cancel");
		    },
		    true));

		flow->add_child(std::move(btns));

		auto flow_ptr = flow.get();
		add_child(std::move(flow));

		this->flow();
		set_width(flow_ptr->width());
		set_height(flow_ptr->height());

		set_focus_by_name("btn_save_all");
	}

	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override
	{
		if (ev.type == event_type::key_press || ev.type == event_type::mouse_click) {
			if (countdown_active_) {
				countdown_active_ = false; // Any interaction cancels countdown
				countdown_label_->set_text("");
			}
		}
		if (ev.type == event_type::key_press && ev.key_code == 27) { // ESC instantly exits per user request
			set_action(dialog_result::confirmed);
			set_result("exit");
			return true;
		}
		return dialog::handle_event(ev, abs_x, abs_y);
	}

	bool tick() override
	{
		if (countdown_active_) {
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
			int new_remaining = 5 - static_cast<int>(elapsed);
			if (new_remaining <= 0) {
				set_action(dialog_result::confirmed);
				set_result("exit");
				return true;
			}
			if (new_remaining != remaining_seconds_) {
				remaining_seconds_ = new_remaining;
				countdown_label_->set_text(std::format("(Auto-closing in {}s)", remaining_seconds_));
			}
		}
		return false;
	}

      private:
	bool countdown_active_{true};
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	int remaining_seconds_{5};
	ui_text_label *countdown_label_{nullptr};
};

std::unique_ptr<dialog> create_plan_approval_dialog(const std::string &plan_text)
{
	int width = std::min(80, COLS);
	int height = std::min(30, LINES - 2);
	if (height < 15) {
		height = 15;
	}

	int reserved = 7;
	int available = height - reserved;
	int feedback_height = std::max(3, available / 4);
	int plan_height = std::max(4, available - feedback_height);

	auto dlg = std::make_unique<dialog>("Approve Plan", width, height);

	auto flow = std::make_unique<ui_vertical_flow>("plan_flow", 2, 1);

	flow->add_child(std::make_unique<ui_text_label>("Proposed Plan:"));

	// Use a multiline edit for the plan text so it is scrollable
	auto plan_box = std::make_unique<ui_multiline_edit>("plan_text", width - 4, plan_height, nullptr);
	plan_box->set_buffer(plan_text);
	flow->add_child(std::move(plan_box));

	flow->add_child(std::make_unique<ui_text_label>("Comments / Feedback (optional if approving, required if rejecting):"));

	auto feedback_box = std::make_unique<ui_multiline_edit>("feedback", width - 4, feedback_height, nullptr);
	flow->add_child(std::move(feedback_box));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_approve", "Approve", 'A', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("Approved");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_reject", "Reject", 'R', [d = dlg.get()]() {
		auto fb = d->get_value("feedback");
		if (fb && !fb->empty()) {
			d->set_action(dialog_result::confirmed); // Confirming the dialog closes it
			d->set_result(*fb);			 // Send feedback as result
		} else {
			// Cannot reject without feedback
		}
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_result("cancel");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("btn_approve");
	return dlg;
}

std::unique_ptr<dialog> create_force_quit_dialog()
{
	return std::make_unique<force_quit_dialog_impl>();
}

std::unique_ptr<dialog> create_ask_user_dialog(const std::string &question, const std::vector<std::string> &options)
{
	std::string trimmed_q = question;
	utf8::trim_trailing_whitespace(trimmed_q);
	while (!trimmed_q.empty() && isspace(trimmed_q.front()))
		trimmed_q.erase(0, 1);

	int max_x = getmaxx(stdscr);
	int max_text_width = std::max(40, max_x - 16);

	std::vector<std::string> lines;
	std::string current_line;
	std::string word;
	for (char c : trimmed_q) {
		if (isspace(c)) {
			if (!word.empty()) {
				if (!current_line.empty() && current_line.length() + 1 + word.length() > (size_t)max_text_width) {
					lines.push_back(current_line);
					current_line = word;
				} else {
					if (!current_line.empty())
						current_line += " ";
					current_line += word;
				}
				word.clear();
			}
			if (c == '\n') {
				lines.push_back(current_line);
				current_line.clear();
			}
		} else {
			word += c;
		}
	}
	if (!word.empty()) {
		if (!current_line.empty() && current_line.length() + 1 + word.length() > (size_t)max_text_width) {
			lines.push_back(current_line);
			current_line = word;
		} else {
			if (!current_line.empty())
				current_line += " ";
			current_line += word;
		}
	}
	if (!current_line.empty())
		lines.push_back(current_line);

	size_t max_line_len = 0;
	for (const auto &l : lines) {
		if (l.length() > max_line_len)
			max_line_len = l.length();
	}

	int width = std::max<int>(70, static_cast<int>(max_line_len) + 12);
	if (width > max_x - 4) {
		width = std::max(70, max_x - 4);
	}

	auto opt_group = std::make_unique<tools::ui_ask_user_group>("options", 0, 0, width - 4, options);

	auto dlg = std::make_unique<dialog>("Question", width, 20);

	auto flow = std::make_unique<ui_vertical_flow>("ask_user_flow", 2, 1);

	for (size_t i = 0; i < lines.size(); ++i) {
		auto label = std::make_unique<ui_text_label>(lines[i], true);
		label->set_width(width - 4);
		flow->add_child(std::move(label));
	}

	opt_group->set_width(width - 4);
	flow->add_child(std::move(opt_group));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'O', [d = dlg.get()]() {
		auto opt = d->get_value("options");
		if (opt)
			d->set_result(*opt);
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));
	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("options");
	return dlg;
}

search_params extract_search_params(const dialog &dlg, const search_params &initial_params)
{
	search_params params = initial_params;

	auto q = dlg.get_value("query");
	if (q)
		params.query = *q;

	auto r = dlg.get_value("replacement");
	if (r)
		params.replacement = *r;

	auto ic = dlg.get_value("ignore_case");
	if (ic)
		params.ignore_case = (*ic == "false"); // Checkbox is "Case sensitive", so true means ignore_case = false

	auto ww = dlg.get_value("whole_words");
	if (ww)
		params.whole_words = (*ww == "true");

	auto re = dlg.get_value("regex");
	if (re)
		params.regex = (*re == "true");

	auto pr = dlg.get_value("prompt_on_replace");
	if (pr)
		params.prompt_on_replace = (*pr == "true");

	auto dir = dlg.get_value("direction");
	if (dir)
		params.backward = (*dir == "dir_backward");

	auto scope = dlg.get_value("scope");
	if (scope)
		params.selected_text_only = (*scope == "scope_selected");

	auto origin = dlg.get_value("origin");
	if (origin)
		params.from_cursor = (*origin == "origin_cursor");

	return params;
}

std::unique_ptr<dialog> create_search_dialog(const std::string &title, const search_params &initial_params, bool is_replace)
{
	int height = is_replace ? 18 : 16;
	auto dlg = std::make_unique<dialog>(title, 64, height);

	auto flow = std::make_unique<ui_vertical_flow>("search_flow", 2, 2);

	// Query
	auto query_tb = std::make_unique<ui_textbox>(
	    "query", 54, initial_params.query,
	    [d = dlg.get()](const std::string &) {
		    d->set_action(dialog_result::confirmed);
		    d->set_result("ok");
	    },
	    "Text to find");
	query_tb->set_history_enabled(true, "search_query");
	flow->add_child(std::move(query_tb));

	// Replace
	if (is_replace) {
		auto replace_tb = std::make_unique<ui_textbox>(
		    "replacement", 54, initial_params.replacement,
		    [d = dlg.get()](const std::string &) {
			    d->set_action(dialog_result::confirmed);
			    d->set_result("ok");
		    },
		    "Replace with");
		replace_tb->set_history_enabled(true, "replace_query");
		flow->add_child(std::move(replace_tb));
	}

	// Options Group
	auto opt_group = std::make_unique<ui_group_box>("opt_group", 30, "Options");
	auto opt_checkboxes = std::make_unique<ui_checkbox_group>("opt_checkboxes");
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("ignore_case", "Case sensitive", 'c', !initial_params.ignore_case));
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("whole_words", "Whole words only", 'w', initial_params.whole_words));
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("regex", "Regular expression", 'r', initial_params.regex));
	if (is_replace) {
		opt_checkboxes->add_child(
		    std::make_unique<ui_checkbox>("prompt_on_replace", "Prompt on replace", 'p', initial_params.prompt_on_replace));
	}
	opt_group->add_child(std::move(opt_checkboxes));

	// Direction Group
	auto dir_group = std::make_unique<ui_group_box>("dir_group", 28, "Direction");
	auto dir_radio = std::make_unique<ui_radiobutton_group>("direction");
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_forward", "Forward", 'f', !initial_params.backward));
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_backward", "Backward", 'b', initial_params.backward));
	dir_group->add_child(std::move(dir_radio));

	// Row 1 (Options & Direction)
	auto row1 = std::make_unique<ui_horizontal_flow>("row1");
	row1->add_child(std::move(opt_group));
	row1->add_child(std::move(dir_group));
	flow->add_child(std::move(row1));

	// Scope Group
	auto scope_group = std::make_unique<ui_group_box>("scope_group", 30, "Scope");
	auto scope_radio = std::make_unique<ui_radiobutton_group>("scope");
	scope_radio->add_child(std::make_unique<ui_radio_choice>("scope_global", "Global", 'g', !initial_params.selected_text_only));
	scope_radio->add_child(
	    std::make_unique<ui_radio_choice>("scope_selected", "Selected text", 's', initial_params.selected_text_only));
	scope_group->add_child(std::move(scope_radio));

	// Origin Group
	auto orig_group = std::make_unique<ui_group_box>("orig_group", 28, "Origin");
	auto orig_radio = std::make_unique<ui_radiobutton_group>("origin");
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_cursor", "From cursor", 'o', initial_params.from_cursor));
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_entire", "Entire scope", 'e', !initial_params.from_cursor));
	orig_group->add_child(std::move(orig_radio));

	// Row 2 (Scope & Origin)
	auto row2 = std::make_unique<ui_horizontal_flow>("row2");
	row2->add_child(std::move(scope_group));
	row2->add_child(std::move(orig_group));
	flow->add_child(std::move(row2));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'k', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

	if (is_replace) {
		btns->add_child(std::make_unique<ui_button>("btn_change_all", "Change all", 'a', [d = dlg.get()]() {
			d->set_action(dialog_result::confirmed);
			d->set_result("change_all");
		}));
	}

	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'l',
	    [d = dlg.get()]() {
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));

	flow_ptr->add_child(std::move(btns));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("query");

	return dlg;
}

std::unique_ptr<dialog> create_maybe_binary_prompt_dialog(const std::string &filename)
{
	int max_dlg_width = COLS > 8 ? COLS - 8 : 50;
	if (max_dlg_width > 120)
		max_dlg_width = 120;

	std::string display_name = filename;
	int msg_overhead = 40; // Length of prefix/suffix around name
	if (static_cast<int>(display_name.length()) + msg_overhead + 4 > max_dlg_width) {
		int max_filename_len = max_dlg_width - msg_overhead - 4;
		if (max_filename_len < 10)
			max_filename_len = 10;
		display_name = fs_utils::shorten_filename(display_name, max_filename_len);
	}

	int desired_width = std::max(55, static_cast<int>(display_name.length()) + msg_overhead + 4);
	auto dlg = std::make_unique<dialog>("Binary Warning", desired_width, 8);

	auto flow = std::make_unique<ui_vertical_flow>("maybe_binary_flow", 2, 2);

	std::string msg = "File '" + display_name + "' contains null bytes. Open as Text or Hex?";
	auto label = std::make_unique<ui_text_label>(msg, true);
	label->set_width(desired_width - 4);
	flow->add_child(std::move(label));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_text", "Text", 'T', [d = dlg.get()]() {
		d->set_result("text");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>("btn_hex", "Hex", 'H', [d = dlg.get()]() {
		d->set_result("hex");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_result("cancel");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));
	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));
	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());
	dlg->set_focus_by_name("btn_text");
	return dlg;
}

std::unique_ptr<dialog> create_reload_prompt_dialog(const std::string &filename)
{
	int max_dlg_width = COLS > 8 ? COLS - 8 : 50;
	if (max_dlg_width > 120)
		max_dlg_width = 120;

	int msg_overhead = 35; // Length of "File " + " has changed on disk. Reload?"

	std::string display_name = filename;
	if (static_cast<int>(display_name.length()) + msg_overhead + 4 > max_dlg_width) {
		int max_filename_len = max_dlg_width - msg_overhead - 4;
		if (max_filename_len < 10)
			max_filename_len = 10;
		display_name = fs_utils::shorten_filename(display_name, max_filename_len);
	}

	int desired_width = std::max(55, static_cast<int>(display_name.length()) + msg_overhead + 4);

	auto dlg = std::make_unique<dialog>("File Changed", desired_width, 8);

	auto flow = std::make_unique<ui_vertical_flow>("reload_flow", 2, 2);

	std::string msg = "File " + display_name + " has changed on disk. Reload?";
	auto label = std::make_unique<ui_text_label>(msg, true);
	label->set_width(desired_width - 4);
	flow->add_child(std::move(label));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_yes", "Yes", 'Y', [d = dlg.get()]() {
		d->set_result("yes");
		d->set_action(dialog_result::confirmed);
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_no", "No", 'N',
	    [d = dlg.get()]() {
		    d->set_result("no");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));
	btns->add_child(std::make_unique<ui_button>("btn_never", "Never", 'v', [d = dlg.get()]() {
		d->set_result("never");
		d->set_action(dialog_result::confirmed);
	}));
	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(std::max(desired_width, flow_ptr->width()));
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("btn_yes");

	return dlg;
}
