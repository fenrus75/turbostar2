#include "ui/dialog_factories.h"
#include <cstdlib>
#include <ncurses.h>
#include "agentlib/ai_model.h"
#include "config_manager.h"
#include "fs_utils.h"
#include "markdown_utils.h"
#include "project_manager.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_dropdown.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_multiline_edit.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_textbox.h"

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
	int total_btn_width = 8 + 2 + 9 + 2 + 8; // 29 chars total
	int btn_start_x = (desired_width - total_btn_width) / 2;

	dlg->add_child(std::make_unique<ui_button>("btn_save", btn_start_x, by, "  Save  ", 'S', [d = dlg.get()]() {
		d->set_result("save");
		d->set_action(dialog_result::confirmed);
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_discard", btn_start_x + 10, by, " Discard ", 'D', [d = dlg.get()]() {
		d->set_result("discard");
		d->set_action(dialog_result::confirmed);
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", btn_start_x + 21, by, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_result("cancel");
		d->set_action(dialog_result::cancelled);
	}));

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

	std::string ok_text = "  OK  ";
	int btn_x = (width - static_cast<int>(ok_text.length())) / 2;
	int btn_y = height - 3;
	dlg->add_child(std::make_unique<ui_button>("btn_ok", btn_x, btn_y, ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

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

	std::string ok_text = "  OK  ";
	int btn_x = (width - static_cast<int>(ok_text.length())) / 2;
	int btn_y = height - 3;
	dlg->add_child(std::make_unique<ui_button>("btn_ok", btn_x, btn_y, ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("ok");
	}));

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
		add_child(std::make_unique<ui_button>("btn_exit", 4, by, "  Exit  ", 'E', [this]() {
			set_action(dialog_result::confirmed);
			set_result("exit");
		}));
		add_child(std::make_unique<ui_button>("btn_save_all", 16, by, " Save All ", 'S', [this]() {
			set_action(dialog_result::confirmed);
			set_result("save_all");
		}));
		add_child(std::make_unique<ui_button>("btn_cancel", 32, by, " Cancel ", 'C', [this]() {
			set_action(dialog_result::cancelled);
			set_result("cancel");
		}));

		set_focus_by_name("btn_save_all");
	}

	void draw(int abs_x, int abs_y) const override
	{
		dialog::draw(abs_x, abs_y);
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
	int btn_x_center = width / 2;

	dlg->add_child(std::make_unique<ui_button>("btn_approve", btn_x_center - 20, btn_y, " Approve ", 'A', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("Approved");
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_reject", btn_x_center - 5, btn_y, " Reject ", 'R', [d = dlg.get()]() {
		auto fb = d->get_value("feedback");
		if (fb && !fb->empty()) {
			d->set_action(dialog_result::confirmed); // Confirming the dialog closes it
			d->set_result(*fb);			 // Send feedback as result
		} else {
			// Cannot reject without feedback
		}
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_cancel", btn_x_center + 10, btn_y, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

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
	markdown_utils::trim_trailing_whitespace(trimmed_q);
	while (!trimmed_q.empty() && isspace(trimmed_q.front()))
		trimmed_q.erase(0, 1);

	std::vector<std::string> lines;
	size_t start = 0;
	size_t end;
	while ((end = trimmed_q.find('\n', start)) != std::string::npos) {
		lines.push_back(trimmed_q.substr(start, end - start));
		start = end + 1;
	}
	lines.push_back(trimmed_q.substr(start));

	size_t max_line_len = 0;
	for (const auto &l : lines) {
		if (l.length() > max_line_len)
			max_line_len = l.length();
	}

	int width = std::max<int>(70, max_line_len + 12);

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
	int btn_x_center = width / 2;

	dlg->add_child(std::make_unique<ui_button>("btn_ok", btn_x_center - 12, btn_y, "   OK   ", 'O', [d = dlg.get()]() {
		auto opt = d->get_value("options");
		if (opt)
			d->set_result(*opt);
		d->set_action(dialog_result::confirmed);
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_cancel", btn_x_center + 2, btn_y, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

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

	int y_off = is_replace ? 2 : 0;

	// Query
	dlg->add_child(std::make_unique<ui_text_label>(2, 2, "Text to find"));
	dlg->add_child(std::make_unique<ui_textbox>("query", 16, 2, 40, initial_params.query, [d = dlg.get()](const std::string &) {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

	// Replace
	if (is_replace) {
		dlg->add_child(std::make_unique<ui_text_label>(2, 4, "Replace with"));
		dlg->add_child(std::make_unique<ui_textbox>("replacement", 16, 4, 40, initial_params.replacement,
							    [d = dlg.get()](const std::string &) {
								    d->set_action(dialog_result::confirmed);
								    d->set_result("ok");
							    }));
	}

	// Options Group
	int opt_h = is_replace ? 5 : 4;
	auto opt_group = std::make_unique<ui_group_box>("opt_group", 2, 5 + y_off, 30, opt_h, "Options");
	opt_group->add_child(std::make_unique<ui_checkbox>("ignore_case", 2, 1, "Case sensitive", 'c', !initial_params.ignore_case));
	opt_group->add_child(std::make_unique<ui_checkbox>("whole_words", 2, 2, "Whole words only", 'w', initial_params.whole_words));
	opt_group->add_child(std::make_unique<ui_checkbox>("regex", 2, 3, "Regular expression", 'r', initial_params.regex));
	if (is_replace) {
		opt_group->add_child(
		    std::make_unique<ui_checkbox>("prompt_on_replace", 2, 4, "Prompt on replace", 'p', initial_params.prompt_on_replace));
	}
	dlg->add_child(std::move(opt_group));

	// Direction Group
	auto dir_group = std::make_unique<ui_group_box>("dir_group", 33, 5 + y_off, 28, 3, "Direction");
	auto dir_radio = std::make_unique<ui_radiobutton_group>("direction", 0, 0, 28, 3);
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_forward", 2, 1, "Forward", 'f', !initial_params.backward));
	dir_radio->add_child(std::make_unique<ui_radio_choice>("dir_backward", 2, 2, "Backward", 'b', initial_params.backward));
	dir_group->add_child(std::move(dir_radio));
	dlg->add_child(std::move(dir_group));

	// Scope Group
	auto scope_group = std::make_unique<ui_group_box>("scope_group", 2, 10 + y_off, 30, 3, "Scope");
	auto scope_radio = std::make_unique<ui_radiobutton_group>("scope", 0, 0, 30, 3);
	scope_radio->add_child(std::make_unique<ui_radio_choice>("scope_global", 2, 1, "Global", 'g', !initial_params.selected_text_only));
	scope_radio->add_child(
	    std::make_unique<ui_radio_choice>("scope_selected", 2, 2, "Selected text", 's', initial_params.selected_text_only));
	scope_group->add_child(std::move(scope_radio));
	dlg->add_child(std::move(scope_group));

	// Origin Group
	auto orig_group = std::make_unique<ui_group_box>("orig_group", 33, 10 + y_off, 28, 3, "Origin");
	auto orig_radio = std::make_unique<ui_radiobutton_group>("origin", 0, 0, 28, 3);
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_cursor", 2, 1, "From cursor", 'o', initial_params.from_cursor));
	orig_radio->add_child(std::make_unique<ui_radio_choice>("origin_entire", 2, 2, "Entire scope", 'e', !initial_params.from_cursor));
	orig_group->add_child(std::move(orig_radio));
	dlg->add_child(std::move(orig_group));

	// Buttons
	int btn_y = 14 + y_off;
	dlg->add_child(std::make_unique<ui_button>("btn_ok", 8, btn_y, "  OK  ", 'k', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

	if (is_replace) {
		dlg->add_child(std::make_unique<ui_button>("btn_change_all", 18, btn_y, " Change all ", 'a', [d = dlg.get()]() {
			d->set_action(dialog_result::confirmed);
			d->set_result("change_all");
		}));
	}

	int bx_pos = is_replace ? 34 : 28;
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", bx_pos, btn_y, " Cancel ", 'l', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

	dlg->set_focus_by_name("query");

	return dlg;
}

#include "config_manager.h"

std::unique_ptr<dialog> create_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Preferences", 60, 24);

	// Clang Format Style group
	auto style_group = std::make_unique<ui_group_box>("style_group", 4, 2, 30, 9, " Clang Format Style ");
	auto style_radio = std::make_unique<ui_radiobutton_group>("style", 0, 0, 30, 9);

	std::vector<std::pair<std::string, char>> style_labels = {
	    {"LLVM", 'L'},   {"Google", 'G'},	 {"Chromium", 'C'}, {"Mozilla", 'M'},
	    {"WebKit", 'W'}, {"Microsoft", 's'}, {"GNU", 'N'},	    {".clang-format file", 'f'}};

	std::string current_style = config_manager::get_instance().get_clang_format_style();
	for (size_t i = 0; i < style_labels.size(); ++i) {
		bool selected = (current_style == style_labels[i].first);
		style_radio->add_child(std::make_unique<ui_radio_choice>(style_labels[i].first, 2, 1 + i, style_labels[i].first,
									 style_labels[i].second, selected));
	}
	style_group->add_child(std::move(style_radio));
	dlg->add_child(std::move(style_group));

	// Build System group
	auto build_group = std::make_unique<ui_group_box>("build_group", 36, 2, 20, 5, " Build System ");
	auto build_radio = std::make_unique<ui_radiobutton_group>("build_system", 0, 0, 20, 5);

	std::vector<std::pair<std::string, char>> system_labels = {{"meson", 'm'}, {"cmake", 'k'}, {"make", 'a'}};

	std::string current_system = config_manager::get_instance().get_build_system();
	for (size_t i = 0; i < system_labels.size(); ++i) {
		bool selected = (current_system == system_labels[i].first);
		build_radio->add_child(std::make_unique<ui_radio_choice>(system_labels[i].first, 2, 1 + i, system_labels[i].first,
									 system_labels[i].second, selected));
	}
	build_group->add_child(std::move(build_radio));
	dlg->add_child(std::move(build_group));

	// Build Directory Input
	dlg->add_child(std::make_unique<ui_text_label>(4, 13, "Build Directory:"));
	dlg->add_child(std::make_unique<ui_textbox>("build_dir", 21, 13, 35, config_manager::get_instance().get_build_directory()));

	// Default Model ID Input
	dlg->add_child(std::make_unique<ui_text_label>(4, 14, "Model ID:"));
	dlg->add_child(std::make_unique<ui_textbox>("default_model_id", 14, 14, 42, config_manager::get_instance().get_default_model_id()));

	// Toggles
	dlg->add_child(std::make_unique<ui_checkbox>("lsp_enabled", 4, 16, "Enable LSP (clangd)", 'E',
						     config_manager::get_instance().is_lsp_enabled()));
	dlg->add_child(std::make_unique<ui_checkbox>("auto_open_error", 4, 17, "Auto-open files for build errors", 'u',
						     config_manager::get_instance().is_auto_open_error_files()));
	dlg->add_child(std::make_unique<ui_checkbox>("compile_on_save", 4, 18, "Compile f[i]le on save", 'i',
						     config_manager::get_instance().is_compile_on_save()));
	dlg->add_child(std::make_unique<ui_checkbox>("log_all_tools", 4, 19, "Log all agent tool calls (debug)", 'g',
						     config_manager::get_instance().is_log_all_tool_calls()));
	dlg->add_child(std::make_unique<ui_checkbox>("software_map", 4, 20, "Auto Software Map (Background LSP)", 'M',
						     config_manager::get_instance().is_software_map_enabled()));

	// Buttons
	dlg->add_child(std::make_unique<ui_button>("btn_ok", 4, 22, " OK (Save Project) ", 'O', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_global", 26, 22, " Save Global ", 'v', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("save_global");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 42, 22, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));
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

		// Buttons
		add_child(std::make_unique<ui_button>("btn_ok", 53, 2, "   Ok   ", 'o', [this]() {
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
		add_child(std::make_unique<ui_button>("btn_cancel", 53, 5, " Cancel ", 'c', [this]() {
			set_action(dialog_result::cancelled);
			set_result("cancel");
		}));

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
	dlg->add_child(std::make_unique<ui_text_label>(2, 13, "Server URL:"));
	dlg->add_child(std::make_unique<ui_textbox>("server_url", 14, 13, 28, "http://localhost:11434/v1"));
	dlg->add_child(std::make_unique<ui_button>("btn_import", 44, 13, " Import ", 'i', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("import");
	}));

	int by = 16;
	dlg->add_child(std::make_unique<ui_button>("btn_add", 4, by, "  Add  ", 'a', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("add");
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_edit", 12, by, "  Edit  ", 'e', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("edit:" + models[idx]->get_id());
			}
		}
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_delete", 22, by, " Delete ", 'd', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("delete:" + models[idx]->get_id());
			}
		}
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_default", 32, by, " Set Default ", 's', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result("default:" + models[idx]->get_id());
			}
		}
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_close", 48, by, " Close ", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

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
	dlg->add_child(std::make_unique<ui_button>("btn_select", 10, by, "  Select  ", 's', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			auto models = agentlib::ai_model_registry::get_instance().get_all_models();
			if (idx < (int)models.size()) {
				d->set_action(dialog_result::confirmed);
				d->set_result(models[idx]->get_id());
			}
		}
	}));

	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 35, by, "  Cancel  ", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

	dlg->set_focus_by_name("model_list");
	return dlg;
}

std::unique_ptr<dialog> create_model_edit_dialog(std::shared_ptr<agentlib::ai_model> model)
{
	auto dlg = std::make_unique<dialog>(model ? "Edit Model" : "Add Model", 64, 20);

	int lx = 4;
	int tx = 16;

	dlg->add_child(std::make_unique<ui_text_label>(lx, 2, "ID:"));
	dlg->add_child(std::make_unique<ui_textbox>("id", tx, 2, 44, model ? model->get_id() : ""));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 3, "Name:"));
	dlg->add_child(std::make_unique<ui_textbox>("name", tx, 3, 44, model ? model->get_name() : ""));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 4, "URL:"));
	dlg->add_child(std::make_unique<ui_textbox>("url", tx, 4, 44, model ? model->get_url() : ""));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 5, "API Key:"));
	dlg->add_child(std::make_unique<ui_textbox>("api_key", tx, 5, 44, model ? model->get_api_key() : ""));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 6, "Purpose:"));
	dlg->add_child(std::make_unique<ui_textbox>("purpose", tx, 6, 44, model ? model->get_purpose() : ""));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 7, "Tx Cost:"));
	dlg->add_child(std::make_unique<ui_textbox>("cost_tx", tx, 7, 10, model ? std::to_string(model->get_cost_per_1m_tx()) : "0.0"));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 8, "Rx Cost:"));
	dlg->add_child(std::make_unique<ui_textbox>("cost_rx", tx, 8, 10, model ? std::to_string(model->get_cost_per_1m_rx()) : "0.0"));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 10, "API Format:"));
	auto type_radio = std::make_unique<ui_radiobutton_group>("api_type", tx, 10, 40, 1);
	bool is_gemini = model && model->get_api_type() == agentlib::api_type::gemini;
	type_radio->add_child(std::make_unique<ui_radio_choice>("openai", 0, 0, " OpenAI ", 'O', !is_gemini));
	type_radio->add_child(std::make_unique<ui_radio_choice>("gemini", 12, 0, " Gemini ", 'G', is_gemini));
	dlg->add_child(std::move(type_radio));

	dlg->add_child(std::make_unique<ui_text_label>(lx, 12, "Cost Model:"));
	auto cost_radio = std::make_unique<ui_radiobutton_group>("cost_type", tx, 12, 45, 1);
	bool is_free = model && model->get_cost_type() == agentlib::model_cost_type::free_local;
	bool is_req = model && model->get_cost_type() == agentlib::model_cost_type::paid_per_request;
	bool is_tok = !is_free && !is_req;
	cost_radio->add_child(std::make_unique<ui_radio_choice>("free_local", 0, 0, " Free ", 'F', is_free));
	cost_radio->add_child(std::make_unique<ui_radio_choice>("paid_per_token", 10, 0, " /Token ", 'T', is_tok));
	cost_radio->add_child(std::make_unique<ui_radio_choice>("paid_per_request", 22, 0, " /Request ", 'R', is_req));
	dlg->add_child(std::move(cost_radio));

	int by = 16;
	dlg->add_child(std::make_unique<ui_button>("btn_ok", 15, by, "   OK   ", 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 35, by, " Cancel ", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

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
		mode_radio->add_child(std::make_unique<ui_radio_choice>(mode_options[i].value, 2, 1 + i, mode_options[i].label,
									mode_options[i].hotkey, selected));
	}
	mode_group->add_child(std::move(mode_radio));
	dlg->add_child(std::move(mode_group));

	// Auto-start debugger checkbox
	bool auto_start = config_manager::get_instance().get_gdb_auto_continue();
	dlg->add_child(
	    std::make_unique<ui_checkbox>("gdb_auto_continue", 4, 11, "Auto-start the application on debugger startup", 'a', auto_start));

	// Buttons
	dlg->add_child(std::make_unique<ui_button>("btn_ok", 4, 13, " OK (Save) ", 'O', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 38, 13, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

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
		std::string check_cmd = "which " + t.command + " > /dev/null 2>&1";
		t.installed = (std::system(check_cmd.c_str()) == 0);
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
	int height = 4 + static_cast<int>(tools.size()) + 1 +
	             (missing_packages.empty() ? 2 : (3 + static_cast<int>(cmd_lines.size()))) +
	             1 + 2;

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

	std::string ok_text = "  OK  ";
	int btn_x = (width - static_cast<int>(ok_text.length())) / 2;
	dlg->add_child(std::make_unique<ui_button>("btn_ok", btn_x, current_y, ok_text, 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

	dlg->set_focus_by_name("btn_ok");
	return dlg;
}
