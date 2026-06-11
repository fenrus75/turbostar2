#include "ui/dialog_factories.h"
#include <cstdlib>
#include <filesystem>
#include <format>
#include <ncurses.h>
#include <chrono>
#include "agentlib/copilot_manager.h"
#include "agentlib/ai_model.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "utf8.h"
#include "mcp/mcp_manager.h"
#include "project_manager.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_buttons_vertical.h"
#include "ui/components/ui_dropdown.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_multiline_edit.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"

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

	std::string msg = "Save changes to " + display_name + "?";
	int text_x = (desired_width - static_cast<int>(msg.length())) / 2;
	dlg->add_child(std::make_unique<ui_text_label>(text_x, 2, msg));

	int by = 8 - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
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
	btns->flow();
	btns->set_position((desired_width - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("btn_save");

	return dlg;
}

std::unique_ptr<dialog> create_message_dialog(const std::string &title, const std::vector<std::string> &lines)
{
	int width = 40;
	for (const auto &line : lines) {
		if (static_cast<int>(line.length()) + 6 > width) {
			width = static_cast<int>(line.length()) + 6;
		}
	}
	int height = lines.size() + 6;
	auto dlg = std::make_unique<dialog>(title, width, height);

	for (size_t i = 0; i < lines.size(); ++i) {
		int text_x = (width - static_cast<int>(lines[i].length())) / 2;
		dlg->add_child(std::make_unique<ui_text_label>(text_x, 2 + i, lines[i]));
	}

	std::string ok_text = "OK";
	int btn_y = height - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, btn_y, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->flow();
	btns->set_position((width - btns->width()) / 2, btn_y);
	dlg->add_child(std::move(btns));

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
};

std::unique_ptr<dialog> create_welcome_dialog()
{
	std::vector<std::string> lines = {"TurboStar", "", "Version " TURBOSTAR_VERSION, "", "Copyright (c) 2026 by", "Arjan van de Ven"};

	int width = 36;
	for (const auto &line : lines) {
		if (static_cast<int>(line.length()) + 6 > width) {
			width = static_cast<int>(line.length()) + 6;
		}
	}
	int height = lines.size() + 6;
	auto dlg = std::make_unique<welcome_dialog_impl>("About", width, height);

	for (size_t i = 0; i < lines.size(); ++i) {
		int text_x = (width - static_cast<int>(lines[i].length())) / 2;
		dlg->add_child(std::make_unique<ui_text_label>(text_x, 2 + i, lines[i]));
	}

	std::string ok_text = "OK";
	int btn_y = height - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, btn_y, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("ok");
	}));
	btns->flow();
	btns->set_position((width - btns->width()) / 2, btn_y);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("btn_ok");
	return dlg;
}

class force_quit_dialog_impl : public dialog
{
      public:
	force_quit_dialog_impl() : dialog("Force Quit", 50, 9)
	{
		start_time_ = std::chrono::steady_clock::now();

		std::string msg = "Unsaved changes! Quit anyway?";
		int text_x = (width_ - static_cast<int>(msg.length())) / 2;
		add_child(std::make_unique<ui_text_label>(text_x, 2, msg));

		int by = height_ - 3;
		auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
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
		btns->flow();
		btns->set_position((width_ - btns->width()) / 2, by);
		add_child(std::move(btns));

		set_focus_by_name("btn_save_all");
	}

	void draw() const override
	{
		dialog::draw();
		if (countdown_active_) {
			std::string count_msg = "(Auto-closing in " + std::to_string(remaining_seconds_) + "s)";
			int count_x = x_ + (width_ - static_cast<int>(count_msg.length())) / 2;
			attron(COLOR_PAIR(1));
			mvaddstr(y_ + 4, count_x, count_msg.c_str());
			attroff(COLOR_PAIR(1));
		}
	}

	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override
	{
		if (ev.type == event_type::key_press || ev.type == event_type::mouse_click) {
			countdown_active_ = false; // Any interaction cancels countdown
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
			remaining_seconds_ = new_remaining;
		}
		return false;
	}

      private:
	bool countdown_active_{true};
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	int remaining_seconds_{5};
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

	dlg->add_child(std::make_unique<ui_text_label>(2, 1, "Proposed Plan:"));

	// Use a multiline edit for the plan text so it is scrollable
	auto plan_box = std::make_unique<ui_multiline_edit>("plan_text", 2, 2, width - 4, plan_height, nullptr);
	plan_box->set_buffer(plan_text);
	dlg->add_child(std::move(plan_box));

	int feedback_y = plan_height + 3;
	dlg->add_child(
	    std::make_unique<ui_text_label>(2, feedback_y, "Comments / Feedback (optional if approving, required if rejecting):"));

	auto feedback_box = std::make_unique<ui_multiline_edit>("feedback", 2, feedback_y + 1, width - 4, feedback_height, nullptr);
	dlg->add_child(std::move(feedback_box));

	int btn_y = height - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, btn_y, 0, 0);
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
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));
	btns->flow();
	btns->set_position((width - btns->width()) / 2, btn_y);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("btn_approve");
	return dlg;
}

std::unique_ptr<dialog> create_force_quit_dialog()
{
	return std::make_unique<force_quit_dialog_impl>();
}

#include "ui/components/ui_ask_user_group.h"
#include "ui/components/ui_text_label.h"

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
	int options_height = opt_group->height();

	// height calculation: 1 (top) + lines.size() + 1 (gap) + options_height + 3 (gap+buttons) + 2 (bottom/shadow)
	int height = 7 + lines.size() + options_height;
	auto dlg = std::make_unique<dialog>("Question", width, height);

	for (size_t i = 0; i < lines.size(); ++i) {
		dlg->add_child(std::make_unique<ui_text_label>((width - lines[i].length()) / 2, 1 + i, lines[i]));
	}

	int start_y = 2 + lines.size();
	opt_group->set_bounds(2, start_y, width - 4, options_height);
	dlg->add_child(std::move(opt_group));

	int btn_y = height - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, btn_y, 0, 0);
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
	btns->flow();
	btns->set_position((width - btns->width()) / 2, btn_y);
	dlg->add_child(std::move(btns));

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

	auto flow = std::make_unique<ui_vertical_flow>("search_flow", 0, 0, 2, 2);

	// Query
	flow->add_child(std::make_unique<ui_textbox>("query", 0, 0, 54, initial_params.query, [d = dlg.get()](const std::string &) {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}, "Text to find"));

	// Replace
	if (is_replace) {
		flow->add_child(std::make_unique<ui_textbox>("replacement", 0, 0, 54, initial_params.replacement,
							    [d = dlg.get()](const std::string &) {
								    d->set_action(dialog_result::confirmed);
								    d->set_result("ok");
							    }, "Replace with"));
	}

	// Options Group
	int opt_h = is_replace ? 5 : 4;
	auto opt_group = std::make_unique<ui_group_box>("opt_group", 0, 0, 30, opt_h, "Options");
	auto opt_checkboxes = std::make_unique<ui_checkbox_group>("opt_checkboxes", 0, 0, 30, opt_h);
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("ignore_case", "Case sensitive", 'c', !initial_params.ignore_case));
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("whole_words", "Whole words only", 'w', initial_params.whole_words));
	opt_checkboxes->add_child(std::make_unique<ui_checkbox>("regex", "Regular expression", 'r', initial_params.regex));
	if (is_replace) {
		opt_checkboxes->add_child(
		    std::make_unique<ui_checkbox>("prompt_on_replace", "Prompt on replace", 'p', initial_params.prompt_on_replace));
	}
	opt_group->add_child(std::move(opt_checkboxes));

	// Direction Group
	auto dir_group = std::make_unique<ui_group_box>("dir_group", 0, 0, 28, 3, "Direction");
	auto dir_radio = std::make_unique<ui_radiobutton_group>("direction", 0, 0, 28, 3);
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_forward", "Forward", 'f', !initial_params.backward));
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_backward", "Backward", 'b', initial_params.backward));
	dir_group->add_child(std::move(dir_radio));

	// Row 1 (Options & Direction)
	auto row1 = std::make_unique<ui_horizontal_flow>("row1", 0, 0, 0, 0);
	row1->add_child(std::move(opt_group));
	row1->add_child(std::move(dir_group));
	flow->add_child(std::move(row1));

	// Scope Group
	auto scope_group = std::make_unique<ui_group_box>("scope_group", 0, 0, 30, 3, "Scope");
	auto scope_radio = std::make_unique<ui_radiobutton_group>("scope", 0, 0, 30, 3);
	scope_radio->add_child(std::make_unique<ui_radio_choice>("scope_global", "Global", 'g', !initial_params.selected_text_only));
	scope_radio->add_child(
	    std::make_unique<ui_radio_choice>("scope_selected", "Selected text", 's', initial_params.selected_text_only));
	scope_group->add_child(std::move(scope_radio));

	// Origin Group
	auto orig_group = std::make_unique<ui_group_box>("orig_group", 0, 0, 28, 3, "Origin");
	auto orig_radio = std::make_unique<ui_radiobutton_group>("origin", 0, 0, 28, 3);
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_cursor", "From cursor", 'o', initial_params.from_cursor));
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_entire", "Entire scope", 'e', !initial_params.from_cursor));
	orig_group->add_child(std::move(orig_radio));

	// Row 2 (Scope & Origin)
	auto row2 = std::make_unique<ui_horizontal_flow>("row2", 0, 0, 0, 0);
	row2->add_child(std::move(scope_group));
	row2->add_child(std::move(orig_group));
	flow->add_child(std::move(row2));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, 0, 0, 0);
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
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("query");

	return dlg;
}

#include "config_manager.h"

std::unique_ptr<dialog> create_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Preferences", 60, 25);

	// Clang Format Style group
	auto style_group = std::make_unique<ui_group_box>("style_group", 0, 0, 30, 9, " Clang Format Style ");
	auto style_radio = std::make_unique<ui_radiobutton_group>("style", 0, 0, 30, 9);

	std::vector<std::pair<std::string, char>> style_labels = {
	    {"LLVM", 'L'},   {"Google", 'G'},	 {"Chromium", 'C'}, {"Mozilla", 'M'},
	    {"WebKit", 'W'}, {"Microsoft", 's'}, {"GNU", 'N'},	    {".clang-format file", 'f'}};

	std::string current_style = config_manager::get_instance().get_clang_format_style();
	for (size_t i = 0; i < style_labels.size(); ++i) {
		bool selected = (current_style == style_labels[i].first);
		style_radio->add_child(std::make_unique<ui_radio_choice>(style_labels[i].first, style_labels[i].first,
									 style_labels[i].second, selected));
	}
	style_group->add_child(std::move(style_radio));

	// Build System group
	auto build_group = std::make_unique<ui_group_box>("build_group", 0, 0, 20, 5, " Build System ");
	auto build_radio = std::make_unique<ui_radiobutton_group>("build_system", 0, 0, 20, 5);

	std::vector<std::pair<std::string, char>> system_labels = {{"meson", 'm'}, {"cmake", 'k'}, {"make", 'a'}};

	std::string current_system = config_manager::get_instance().get_build_system();
	for (size_t i = 0; i < system_labels.size(); ++i) {
		bool selected = (current_system == system_labels[i].first);
		build_radio->add_child(std::make_unique<ui_radio_choice>(system_labels[i].first, system_labels[i].first,
									 system_labels[i].second, selected));
	}
	build_group->add_child(std::move(build_radio));

	// Row 1 (Style & Build Groups)
	auto row1 = std::make_unique<ui_horizontal_flow>("row1", 0, 2, 4, 0);
	row1->add_child(std::move(style_group));
	row1->add_child(std::move(build_group));
	dlg->add_child(std::move(row1));

	// Build Directory Input
	dlg->add_child(std::make_unique<ui_textbox>("build_dir", 4, 13, 52, config_manager::get_instance().get_build_directory(), nullptr, "Build Directory:"));

	// Default Model ID Input
	dlg->add_child(std::make_unique<ui_textbox>("default_model_id", 4, 14, 52, config_manager::get_instance().get_default_model_id(), nullptr, "Model ID:"));

	// Toggles
	auto toggles_group = std::make_unique<ui_checkbox_group>("toggles", 2, 16, 52, 6);
	toggles_group->add_child(std::make_unique<ui_checkbox>("lsp_enabled", "Enable LSP (clangd)", 'E',
						     config_manager::get_instance().is_lsp_enabled()));
	toggles_group->add_child(std::make_unique<ui_checkbox>("auto_open_error", "Auto-open files for build errors", 'u',
						     config_manager::get_instance().is_auto_open_error_files()));
	toggles_group->add_child(std::make_unique<ui_checkbox>("compile_on_save", "Compile f[i]le on save", 'i',
						     config_manager::get_instance().is_compile_on_save()));
	toggles_group->add_child(std::make_unique<ui_checkbox>("log_all_tools", "Log all agent tool calls (debug)", 'g',
						     config_manager::get_instance().is_log_all_tool_calls()));
	toggles_group->add_child(std::make_unique<ui_checkbox>("software_map", "Auto Software Map (Background LSP)", 'M',
						     config_manager::get_instance().is_software_map_enabled()));
	toggles_group->add_child(std::make_unique<ui_checkbox>("shell_display_access", "Give shell tool [d]isplay access", 'd',
						     config_manager::get_instance().is_shell_display_access()));
	dlg->add_child(std::move(toggles_group));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, 23, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK (Save Project)", 'O', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_global", "Save Global", 'v', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("save_global");
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_result("cancel");
		    d->set_action(dialog_result::cancelled);
	    },
	    true));
	btns->flow();
	btns->set_position((60 - btns->width()) / 2, 23);
	dlg->add_child(std::move(btns));
	dlg->set_focus_by_name("style_group");

	return dlg;
}

void apply_settings_from_dialog(const dialog &dlg)
{
	auto &cfg = config_manager::get_instance();

	auto style = dlg.get_value("style");
	if (style)
		cfg.set_clang_format_style(*style);

	auto build_sys = dlg.get_value("build_system");
	if (build_sys)
		cfg.set_build_system(*build_sys);

	auto b_dir = dlg.get_value("build_dir");
	if (b_dir)
		cfg.set_build_directory(*b_dir);

	auto def_model = dlg.get_value("default_model_id");
	if (def_model)
		cfg.set_default_model_id(*def_model);

	auto lsp = dlg.get_value("lsp_enabled");
	if (lsp)
		cfg.set_lsp_enabled(*lsp == "true");

	auto auto_op = dlg.get_value("auto_open_error");
	if (auto_op)
		cfg.set_auto_open_error_files(*auto_op == "true");

	auto cmp = dlg.get_value("compile_on_save");
	if (cmp)
		cfg.set_compile_on_save(*cmp == "true");

	auto log_tools = dlg.get_value("log_all_tools");
	if (log_tools)
		cfg.set_log_all_tool_calls(*log_tools == "true");

	auto sw_map = dlg.get_value("software_map");
	if (sw_map)
		cfg.set_software_map_enabled(*sw_map == "true");

	auto shell_display = dlg.get_value("shell_display_access");
	if (shell_display)
		cfg.set_shell_display_access(*shell_display == "true");

	// Note: We don't save here anymore. The key event handler does it so it knows whether to save global or project.
}
// Ensure the #include "config_manager.h" is not duplicated if it's already there

class file_dialog_impl : public dialog
{
      public:
	file_dialog_impl(const std::string &title, const std::string &initial_path) : dialog(title, 68, 17), initial_path_(initial_path)
	{
		// Name Label
		add_child(std::make_unique<ui_text_label>(2, 2, "Name"));

		auto on_tb_submit = [this](const std::string &val) {
			std::string final_val = val;
			std::string suggestion = get_fs_view()->get_autocomplete_suggestion(val);
			if (!suggestion.empty()) {
				final_val = suggestion;
			}
			if (final_val.empty())
				return;

			fs::path entered_path = get_fs_view()->get_current_path() / final_val;
			if (fs::exists(entered_path) && fs::is_directory(entered_path)) {
				get_fs_view()->set_current_path(fs::canonical(entered_path));
				get_textbox()->set_buffer("");
			} else {
				set_action(dialog_result::confirmed);
				set_result((get_fs_view()->get_current_path() / final_val).string());
			}
		};

		// Entry Box (with autocomplete provider attached!)
		auto tb = std::make_unique<ui_textbox>("filename", 8, 2, 40, "", on_tb_submit);
		tb->set_autocomplete_provider([this](const std::string &buf) { return get_fs_view()->get_autocomplete_suggestion(buf); });
		add_child(std::move(tb));

		// Files Label
		add_child(std::make_unique<ui_text_label>(2, 4, "Files"));

		// Filesystem View
		auto on_fs_change = [this](const std::string &name) { get_textbox()->set_buffer(name); };
		auto on_fs_submit = [this](const std::string &name) {
			set_action(dialog_result::confirmed);
			set_result((get_fs_view()->get_current_path() / name).string());
		};

		add_child(std::make_unique<ui_fileselector>("fileselector", 2, 5, 46, 8, initial_path, on_fs_change, on_fs_submit));
		auto fs_view_ptr = static_cast<ui_fileselector *>(children_.back().get());
		add_child(std::make_unique<ui_file_info_panel>(1, 14, 66, fs_view_ptr));

		auto btns = std::make_unique<ui_buttons_vertical>("buttons", 53, 2, 0, 0);
		btns->add_child(std::make_unique<ui_button>("btn_ok", "Ok", 'o', [this]() {
			std::string val = *get_value("filename");
			if (!val.empty()) {
				fs::path entered_path = get_fs_view()->get_current_path() / val;
				if (fs::is_directory(entered_path)) {
					get_fs_view()->set_current_path(fs::canonical(entered_path));
					get_textbox()->set_buffer("");
					set_focus_by_name("filename");
				} else {
					set_action(dialog_result::confirmed);
					set_result(entered_path.string());
				}
			}
		}));
		btns->add_child(std::make_unique<ui_button>(
		    "btn_cancel", "Cancel", 'c',
		    [this]() {
			    set_action(dialog_result::cancelled);
			    set_result("cancel");
		    },
		    true));
		btns->flow();
		add_child(std::move(btns));

		set_focus_by_name("filename");
		get_textbox()->set_buffer("");
	}

      private:
	ui_fileselector *get_fs_view() const
	{
		for (auto &c : children_) {
			if (c->name() == "fileselector")
				return static_cast<ui_fileselector *>(c.get());
		}
		return nullptr;
	}
	ui_textbox *get_textbox() const
	{
		for (auto &c : children_) {
			if (c->name() == "filename")
				return static_cast<ui_textbox *>(c.get());
		}
		return nullptr;
	}

	std::string initial_path_;
};

std::unique_ptr<dialog> create_file_dialog(const std::string &title, const std::string &initial_path)
{
	return std::make_unique<file_dialog_impl>(title, initial_path);
}

std::unique_ptr<dialog> create_model_list_dialog()
{
	auto dlg = std::make_unique<dialog>("AI Models", 60, 20);
	auto models = agentlib::ai_model_registry::get_instance().get_all_models();
	std::string default_id = config_manager::get_instance().get_default_model_id();

	std::vector<std::string> item_labels;
	for (const auto &m : models) {
		std::string prefix = (m->get_id() == default_id) ? "* " : "  ";
		item_labels.push_back(prefix + m->get_id() + " - " + m->get_name());
	}

	auto on_submit = [d = dlg.get()](int idx) {
		auto models = agentlib::ai_model_registry::get_instance().get_all_models();
		if (idx >= 0 && idx < (int)models.size()) {
			d->set_action(dialog_result::confirmed);
			d->set_result("edit:" + models[idx]->get_id());
		}
	};

	auto lb = std::make_unique<ui_listbox>("model_list", 2, 2, 56, 10, nullptr, on_submit);
	lb->set_items(item_labels);
	auto lb_ptr = lb.get();
	dlg->add_child(std::move(lb));

	// Server URL and Import controls at y = 13
	auto import_row = std::make_unique<ui_horizontal_flow>("import_row", 0, 13, 2, 0);
	import_row->add_child(std::make_unique<ui_text_label>(0, 0, "Server URL:"));
	import_row->add_child(std::make_unique<ui_textbox>("server_url", 0, 0, 28, "http://localhost:11434/v1"));
	import_row->add_child(std::make_unique<ui_button>("btn_import", "Import", 'i', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("import");
	}));
	dlg->add_child(std::move(import_row));

	int by = 16;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_add", "Add", 'a', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("add");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_edit", "Edit", 'e', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("edit:" + models[idx]->get_id());
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_delete", "Delete", 'd', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("delete:" + models[idx]->get_id());
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_default", "Set Default", 's', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("default:" + models[idx]->get_id());
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_close", "Close", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));
	btns->flow();
	btns->set_position((60 - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("model_list");
	return dlg;
}

std::unique_ptr<dialog> create_model_selection_dialog()
{
	auto dlg = std::make_unique<dialog>("Select Model", 60, 18);
	auto models = agentlib::ai_model_registry::get_instance().get_all_models();

	std::vector<std::string> item_labels;
	for (const auto &m : models) {
		item_labels.push_back("  " + m->get_id() + " - " + m->get_name());
	}

	auto on_submit = [d = dlg.get()](int idx) {
		auto models = agentlib::ai_model_registry::get_instance().get_all_models();
		if (idx >= 0 && idx < (int)models.size()) {
			d->set_action(dialog_result::confirmed);
			d->set_result(models[idx]->get_id());
		}
	};

	auto lb = std::make_unique<ui_listbox>("model_list", 2, 2, 56, 10, nullptr, on_submit);
	lb->set_items(item_labels);
	auto lb_ptr = lb.get();
	dlg->add_child(std::move(lb));

	int by = 14;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_select", "Select", 's', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result(models[idx]->get_id());
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'c',
	    [d = dlg.get()]() {
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));
	btns->flow();
	btns->set_position((60 - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("model_list");
	return dlg;
}

std::unique_ptr<dialog> create_model_edit_dialog(std::shared_ptr<agentlib::ai_model> model)
{
	auto dlg = std::make_unique<dialog>(model ? "Edit Model" : "Add Model", 64, 20);

	int lx = 4;
	int tx = 16;

	dlg->add_child(std::make_unique<ui_textbox>("id", lx, 2, 56, model ? model->get_id() : "", nullptr, "ID:        "));
	dlg->add_child(std::make_unique<ui_textbox>("name", lx, 3, 56, model ? model->get_name() : "", nullptr, "Name:      "));
	dlg->add_child(std::make_unique<ui_textbox>("url", lx, 4, 56, model ? model->get_url() : "", nullptr, "URL:       "));
	dlg->add_child(std::make_unique<ui_textbox>("api_key", lx, 5, 56, model ? model->get_api_key() : "", nullptr, "API Key:   "));
	dlg->add_child(std::make_unique<ui_textbox>("purpose", lx, 6, 56, model ? model->get_purpose() : "", nullptr, "Purpose:   "));
	dlg->add_child(std::make_unique<ui_textbox>("cost_tx", lx, 7, 22, model ? std::to_string(model->get_cost_per_1m_tx()) : "0.0", nullptr, "Tx Cost:   "));
	dlg->add_child(std::make_unique<ui_textbox>("cost_rx", lx, 8, 22, model ? std::to_string(model->get_cost_per_1m_rx()) : "0.0", nullptr, "Rx Cost:   "));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 10, "API Format:"));
	auto type_radio = std::make_unique<ui_radiobutton_group>("api_type", tx, 10, 40, 1, true);
	bool is_gemini = model && model->get_api_type() == agentlib::api_type::gemini;
	type_radio->add_child(std::make_unique<ui_radio_choice>("openai", " OpenAI ", 'O', !is_gemini));
	type_radio->add_child(std::make_unique<ui_radio_choice>("gemini", " Gemini ", 'G', is_gemini));
	dlg->add_child(std::move(type_radio));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 12, "Cost Model:"));
	auto cost_radio = std::make_unique<ui_radiobutton_group>("cost_type", tx, 12, 45, 1, true);
	bool is_free = model && model->get_cost_type() == agentlib::model_cost_type::free_local;
	bool is_req = model && model->get_cost_type() == agentlib::model_cost_type::paid_per_request;
	bool is_tok = !is_free && !is_req;
	cost_radio->add_child(std::make_unique<ui_radio_choice>("free_local", " Free ", 'F', is_free));
	cost_radio->add_child(std::make_unique<ui_radio_choice>("paid_per_token", " /Token ", 'T', is_tok));
	cost_radio->add_child(std::make_unique<ui_radio_choice>("paid_per_request", " /Request ", 'R', is_req));
	dlg->add_child(std::move(cost_radio));

	int by = 16;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'c',
	    [d = dlg.get()]() {
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));
	btns->flow();
	btns->set_position((60 - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("id");
	return dlg;
}

void apply_model_edit_from_dialog(const dialog &dlg, const std::string &original_id)
{
	auto id_opt = dlg.get_value("id");
	auto name_opt = dlg.get_value("name");
	auto url_opt = dlg.get_value("url");
	auto api_key_opt = dlg.get_value("api_key");
	auto purpose_opt = dlg.get_value("purpose");
	auto tx_cost_opt = dlg.get_value("cost_tx");
	auto rx_cost_opt = dlg.get_value("cost_rx");
	auto api_type_opt = dlg.get_value("api_type");
	auto cost_type_opt = dlg.get_value("cost_type");

	if (!id_opt || id_opt->empty())
		return;

	double tx_cost = 0.0;
	double rx_cost = 0.0;
	try {
		if (tx_cost_opt)
			tx_cost = std::stod(*tx_cost_opt);
		if (rx_cost_opt)
			rx_cost = std::stod(*rx_cost_opt);
	} catch (...) {
	}

	agentlib::api_type type = agentlib::api_type::openai;
	if (api_type_opt && *api_type_opt == "gemini") {
		type = agentlib::api_type::gemini;
	}

	agentlib::model_cost_type cost_type = agentlib::model_cost_type::paid_per_token;
	if (cost_type_opt) {
		if (*cost_type_opt == "free_local")
			cost_type = agentlib::model_cost_type::free_local;
		else if (*cost_type_opt == "paid_per_request")
			cost_type = agentlib::model_cost_type::paid_per_request;
	}

	auto &registry = agentlib::ai_model_registry::get_instance();

	if (!original_id.empty() && original_id != *id_opt) {
		registry.remove_model(original_id);
	}

	auto model = std::make_shared<agentlib::ai_model>(*id_opt, name_opt ? *name_opt : "", url_opt ? *url_opt : "",
							  purpose_opt ? *purpose_opt : "", tx_cost, rx_cost,
							  api_key_opt ? *api_key_opt : "", type, 250000, cost_type);
	registry.update_model(model);
	registry.save_models();
}

std::unique_ptr<dialog> create_run_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Run Settings", 60, 15);

	// Main Executable Input
	dlg->add_child(std::make_unique<ui_text_label>(4, 2, "Main Executable:"));
	auto candidates = project_manager::get_instance().detect_executable_candidates();
	dlg->add_child(
	    std::make_unique<ui_dropdown>("main_executable", 21, 2, 35, config_manager::get_instance().get_main_executable(), candidates));

	// Arguments Input
	dlg->add_child(std::make_unique<ui_text_label>(4, 4, "Arguments:"));
	dlg->add_child(std::make_unique<ui_textbox>("run_arguments", 21, 4, 35, config_manager::get_instance().get_run_arguments()));

	// Run Target Mode group
	auto mode_group = std::make_unique<ui_group_box>("mode_group", 4, 6, 52, 5, " Run Target Mode ");
	auto mode_radio = std::make_unique<ui_radiobutton_group>("run_target_mode", 0, 0, 52, 5);

	struct mode_opt_t {
		std::string value;
		std::string label;
		char hotkey;
	};
	std::vector<mode_opt_t> mode_options = {
	    {"window", "Run in a text window", 'w'}, {"fullscreen", "Run full screen", 'f'}, {"xterm", "Run in a new X terminal", 'x'}};

	std::string current_mode = config_manager::get_instance().get_run_target_mode();
	if (current_mode != "window" && current_mode != "fullscreen" && current_mode != "xterm") {
		current_mode = "window";
	}

	for (size_t i = 0; i < mode_options.size(); ++i) {
		bool selected = (current_mode == mode_options[i].value);
		mode_radio->add_child(std::make_unique<ui_radio_choice>(mode_options[i].value, mode_options[i].label,
									mode_options[i].hotkey, selected));
	}
	mode_group->add_child(std::move(mode_radio));
	dlg->add_child(std::move(mode_group));

	// Auto-start debugger checkbox
	bool auto_start = config_manager::get_instance().get_gdb_auto_continue();
	auto auto_start_group = std::make_unique<ui_checkbox_group>("auto_start_group", 2, 11, 52, 1);
	auto_start_group->add_child(
	    std::make_unique<ui_checkbox>("gdb_auto_continue", "Auto-start the application on debugger startup", 'a', auto_start));
	dlg->add_child(std::move(auto_start_group));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, 13, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK (Save)", 'O', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->add_child(std::make_unique<ui_button>(
	    "btn_cancel", "Cancel", 'C',
	    [d = dlg.get()]() {
		    d->set_action(dialog_result::cancelled);
		    d->set_result("cancel");
	    },
	    true));
	btns->flow();
	btns->set_position((60 - btns->width()) / 2, 13);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("main_executable");
	return dlg;
}

void apply_run_settings_from_dialog(const dialog &dlg)
{
	auto &cfg = config_manager::get_instance();

	auto main_exe = dlg.get_value("main_executable");
	if (main_exe)
		cfg.set_main_executable(*main_exe);

	auto run_args = dlg.get_value("run_arguments");
	if (run_args)
		cfg.set_run_arguments(*run_args);

	auto target_mode = dlg.get_value("run_target_mode");
	if (target_mode)
		cfg.set_run_target_mode(*target_mode);

	auto gdb_start = dlg.get_value("gdb_auto_continue");
	if (gdb_start)
		cfg.set_gdb_auto_continue(*gdb_start == "true");
}

std::unique_ptr<dialog> create_tool_status_dialog()
{
	struct tool_info {
		std::string name;
		std::string command;
		std::string package;
		bool installed;
	};

	std::vector<tool_info> tools = {{"gdb", "gdb", "gdb", false},
					{"gdbserver", "gdbserver", "gdbserver", false},
					{"clangd", "clangd", "clangd", false},
					{"pylsp", "pylsp", "python3-pylsp", false},
					{"clang-format", "clang-format", "clang-format", false},
					{"bandit", "bandit", "python3-bandit", false},
					{"eu-addr2line", "eu-addr2line", "elfutils", false}};

	std::vector<std::string> missing_packages;
	for (auto &t : tools) {
		std::string path = "/usr/bin/" + t.command;
		t.installed = std::filesystem::exists(path);
		if (!t.installed) {
			missing_packages.push_back(t.package);
		}
	}

	std::vector<std::string> cmd_lines;
	if (!missing_packages.empty()) {
		std::string current_line = "sudo apt install";
		for (const auto &pkg : missing_packages) {
			if (current_line.length() + 1 + pkg.length() > 44) {
				cmd_lines.push_back(current_line);
				current_line = "  " + pkg;
			} else {
				current_line += " " + pkg;
			}
		}
		cmd_lines.push_back(current_line);
	}

	int width = 56;
	int height =
	    4 + static_cast<int>(tools.size()) + 1 + (missing_packages.empty() ? 2 : (3 + static_cast<int>(cmd_lines.size()))) + 1 + 2;

	auto dlg = std::make_unique<dialog>("Tool Status", width, height);

	// Title label
	dlg->add_child(std::make_unique<ui_text_label>(4, 2, "Diagnostic Tool Status:"));

	int current_y = 4;
	for (const auto &t : tools) {
		std::string status_str = t.installed ? "☑ Installed" : "☐ Missing";
		std::string name_col = t.name + ":";
		if (name_col.length() < 15) {
			name_col.append(15 - name_col.length(), ' ');
		}
		dlg->add_child(std::make_unique<ui_text_label>(6, current_y, name_col + status_str));
		current_y++;
	}

	current_y++; // blank line

	if (!missing_packages.empty()) {
		dlg->add_child(std::make_unique<ui_text_label>(4, current_y++, "Some dependencies are missing."));
		dlg->add_child(std::make_unique<ui_text_label>(4, current_y++, "To install them, run:"));

		for (const auto &line : cmd_lines) {
			dlg->add_child(std::make_unique<ui_text_label>(6, current_y++, line));
		}
		current_y++;
	} else {
		dlg->add_child(std::make_unique<ui_text_label>(4, current_y++, "All dependencies are installed!"));
		current_y++;
	}

	std::string ok_text = "OK";
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, current_y, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_ok", ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->flow();
	btns->set_position((width - btns->width()) / 2, current_y);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("btn_ok");
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

	std::string msg = "File " + display_name + " has changed on disk. Reload?";
	int text_x = (desired_width - static_cast<int>(msg.length())) / 2;
	dlg->add_child(std::make_unique<ui_text_label>(text_x, 2, msg));

	int by = 8 - 3;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
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
	btns->flow();
	btns->set_position((desired_width - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("btn_yes");

	return dlg;
}

std::unique_ptr<dialog> create_mcp_config_dialog(int initial_selection)
{
	auto dlg = std::make_unique<dialog>("MCP Servers", 64, 20);
	auto &manager = agentlib::mcp_manager::get_instance();
	const auto &servers = manager.get_servers();

	std::vector<std::string> item_labels;
	for (const auto &server : servers) {
		std::string state_box = server->is_enabled() ? "[X]" : "[ ]";
		std::string type_str = server->is_system() ? "System" : "Project";
		std::string status_str = server->is_running() ? "Running" : "Stopped";
		if (server->is_running() && server->get_startup_time_ms() >= 0) {
			item_labels.push_back(std::format("{} {} ({}, {}, {}ms)", state_box, server->get_name(), type_str, status_str, server->get_startup_time_ms()));
		} else {
			item_labels.push_back(std::format("{} {} ({}, {})", state_box, server->get_name(), type_str, status_str));
		}
	}

	auto lb = std::make_unique<ui_listbox>("mcp_server_list", 2, 2, 60, 12, nullptr, nullptr);
	lb->set_items(item_labels);
	lb->set_selected_index(initial_selection);
	auto lb_ptr = lb.get();

	lb->set_on_space([d = dlg.get(), lb_ptr](int /*idx*/) {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto &manager = agentlib::mcp_manager::get_instance();
			const auto &servers = manager.get_servers();
			if (idx < (int)servers.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result(std::format("toggle:{}", servers[idx]->get_name()));
			}
		}
	});

	dlg->add_child(std::move(lb));

	int by = 17;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_toggle", "Toggle", 't', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto &manager = agentlib::mcp_manager::get_instance();
			const auto &servers = manager.get_servers();
			if (idx < (int)servers.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result(std::format("toggle:{}", servers[idx]->get_name()));
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_tools", "Tools...", 'o', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto &manager = agentlib::mcp_manager::get_instance();
			const auto &servers = manager.get_servers();
			if (idx < (int)servers.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result(std::format("tools:{}", servers[idx]->get_name()));
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_close", "Close", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));
	btns->flow();
	btns->set_position((64 - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("mcp_server_list");
	return dlg;
}

std::unique_ptr<dialog> create_mcp_tools_dialog(const std::string &server_name, int initial_selection)
{
	auto dlg = std::make_unique<dialog>(std::format("Tools - {}", server_name), 64, 20);
	auto server = agentlib::mcp_manager::get_instance().find_server(server_name);
	if (!server) {
		dlg->add_child(std::make_unique<ui_text_label>(2, 2, "Error: Server not found."));
		dlg->add_child(std::make_unique<ui_button>("btn_close", 26, 17, " Close ", 'c', [d = dlg.get()]() {
			d->set_action(dialog_result::cancelled);
			d->set_result("cancel");
		}));
		return dlg;
	}

	auto tools = server->get_tools();
	std::vector<std::string> item_labels;
	for (const auto &tool : tools) {
		std::string state_box = tool.enabled ? "[X]" : "[ ]";
		item_labels.push_back(std::format("{} {} - {}", state_box, tool.name, tool.description));
	}

	auto lb = std::make_unique<ui_listbox>("mcp_tool_list", 2, 2, 60, 12, nullptr, nullptr);
	lb->set_items(item_labels);
	lb->set_selected_index(initial_selection);
	auto lb_ptr = lb.get();

	lb->set_on_space([d = dlg.get(), lb_ptr, server_name](int /*idx*/) {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto server = agentlib::mcp_manager::get_instance().find_server(server_name);
			if (server) {
				auto tools = server->get_tools();
				if (idx < (int)tools.size()) {
					d->set_action(dialog_result::confirmed);
					d->set_result(std::format("toggle:{}", tools[idx].name));
				}
			}
		}
	});

	dlg->add_child(std::move(lb));

	int by = 17;
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
	btns->add_child(std::make_unique<ui_button>("btn_toggle", "Toggle", 't', [d = dlg.get(), lb_ptr, server_name]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto server = agentlib::mcp_manager::get_instance().find_server(server_name);
			if (server) {
				auto tools = server->get_tools();
				if (idx < (int)tools.size()) {
					d->set_action(dialog_result::confirmed);
					d->set_result(std::format("toggle:{}", tools[idx].name));
				}
			}
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_back", "Back", 'b', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("back");
	}));
	btns->flow();
	btns->set_position((64 - btns->width()) / 2, by);
	dlg->add_child(std::move(btns));

	dlg->set_focus_by_name("mcp_tool_list");
	return dlg;
}

class copilot_connect_dialog_impl : public dialog
{
public:
	copilot_connect_dialog_impl() : dialog("Copilot Connect", 60, 13)
	{
		std::string user_code, verification_uri;
		bool success = agentlib::copilot_manager::get_instance().start_device_flow(user_code, verification_uri);

		if (success) {
			user_code_ = user_code;
			verification_uri_ = verification_uri;
			status_ = "Waiting for GitHub authentication...";
			initialized_ = true;
		} else {
			status_ = "Failed to start Device Flow. Check connection.";
			initialized_ = false;
		}

		int by = height_ - 3;
		auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, by, 0, 0);
		btns->add_child(std::make_unique<ui_button>(
			"btn_cancel", "Cancel", 'C',
			[this]() {
				set_action(dialog_result::cancelled);
				set_result("cancel");
			},
			true));
		btns->flow();
		btns->set_position((width_ - btns->width()) / 2, by);
		add_child(std::move(btns));

		set_focus_by_name("btn_cancel");
		last_poll_time_ = std::chrono::steady_clock::now();
	}

	void draw() const override
	{
		dialog::draw();

		int dy = y_ + 2;
		if (initialized_) {
			std::string msg1 = "To connect GitHub Copilot, please visit:";
			mvaddstr(dy++, x_ + (width_ - static_cast<int>(msg1.length())) / 2, msg1.c_str());

			attron(COLOR_PAIR(2) | A_BOLD);
			mvaddstr(dy++, x_ + (width_ - static_cast<int>(verification_uri_.length())) / 2, verification_uri_.c_str());
			attroff(COLOR_PAIR(2) | A_BOLD);

			dy++;
			std::string msg2 = "And enter the following code:";
			mvaddstr(dy++, x_ + (width_ - static_cast<int>(msg2.length())) / 2, msg2.c_str());

			attron(COLOR_PAIR(1) | A_BOLD);
			mvaddstr(dy++, x_ + (width_ - static_cast<int>(user_code_.length())) / 2, user_code_.c_str());
			attroff(COLOR_PAIR(1) | A_BOLD);

			dy++;
		} else {
			dy += 4;
		}

		attron(COLOR_PAIR(1));
		mvaddstr(dy++, x_ + (width_ - static_cast<int>(status_.length())) / 2, status_.c_str());
		attroff(COLOR_PAIR(1));
	}

	bool tick() override
	{
		if (!initialized_) {
			return false;
		}

		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_poll_time_).count();

		int interval = agentlib::copilot_manager::get_instance().get_polling_interval();
		// Add a 1-second safety buffer to avoid clock skew slow_downs
		if (elapsed >= interval + 1) {
			last_poll_time_ = now;
			status_ = "Polling GitHub for authorization...";
			event_logger::get_instance().log("Copilot Connect Dialog tick: Polling GitHub (interval={}s, buffer=1s)...", interval);
			
			bool authenticated = agentlib::copilot_manager::get_instance().poll_device_authorization(interval);
			event_logger::get_instance().log("Copilot Connect Dialog tick: Poll result authenticated = {}", authenticated);
			if (authenticated) {
				status_ = "Successfully connected to Copilot!";
				set_action(dialog_result::confirmed);
				set_result("success");
				return true;
			} else {
				status_ = "Waiting for GitHub authentication...";
			}
		}

		return false;
	}

private:
	bool initialized_{false};
	std::string user_code_;
	std::string verification_uri_;
	std::string status_;
	std::chrono::time_point<std::chrono::steady_clock> last_poll_time_;
};

std::unique_ptr<dialog> create_copilot_connect_dialog()
{
	return std::make_unique<copilot_connect_dialog_impl>();
}
