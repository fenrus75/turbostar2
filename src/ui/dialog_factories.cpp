#include "ui/dialog_factories.h"
#include "config_manager.h"
#include "fs_utils.h"
#include <ncurses.h>

std::unique_ptr<dialog> create_save_prompt_dialog(const std::string& filename) {
	auto dlg = std::make_unique<dialog>("Unsaved Changes", 50, 8);
	
	std::string msg = "Save changes to " + filename + "?";
	int text_x = (50 - static_cast<int>(msg.length())) / 2;
	dlg->add_child(std::make_unique<ui_text_label>(text_x, 2, msg));
	
	int by = 8 - 3;
	dlg->add_child(std::make_unique<ui_button>("btn_save", 4, by, "  Save  ", 'S', [d = dlg.get()]() {
		d->set_result("save");
		d->set_action(dialog_result::confirmed);
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_discard", 18, by, " Discard ", 'D', [d = dlg.get()]() {
		d->set_result("discard");
		d->set_action(dialog_result::confirmed);
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 34, by, " Cancel ", 'C', [d = dlg.get()]() {
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

class force_quit_dialog_impl : public dialog {
public:
	force_quit_dialog_impl() : dialog("Force Quit", 50, 9) {
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

	void draw(int abs_x, int abs_y) const override {
		dialog::draw(abs_x, abs_y);
		if (countdown_active_) {
			std::string count_msg = "(Auto-closing in " + std::to_string(remaining_seconds_) + "s)";
			int count_x = x_ + (width_ - static_cast<int>(count_msg.length())) / 2;
			attron(COLOR_PAIR(1));
			mvaddstr(y_ + 4, count_x, count_msg.c_str());
			attroff(COLOR_PAIR(1));
		}
	}
	
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override {
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
	
	bool tick() override {
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

std::unique_ptr<dialog> create_force_quit_dialog()
{
	return std::make_unique<force_quit_dialog_impl>();
}

std::unique_ptr<dialog> create_ask_user_dialog(const std::string& question, const std::vector<std::string>& options)
{
	int height = 9 + options.size();
	int width = std::max<int>(60, question.length() + 6);
	auto dlg = std::make_unique<dialog>("Question", width, height);

	dlg->add_child(std::make_unique<ui_text_label>((width - question.length()) / 2, 2, question));

	auto opt_group = std::make_unique<ui_radiobutton_group>("options", 3, 4, width - 6, options.size());
	for (size_t i = 0; i < options.size(); ++i) {
		std::string label = options[i];
		if (label.length() > static_cast<size_t>(width - 10)) label = label.substr(0, width - 10);
		opt_group->add_child(std::make_unique<ui_radio_choice>(options[i], 0, i, label, '\0', i == 0));
	}
	dlg->add_child(std::move(opt_group));

	int text_y = 4 + options.size();
	dlg->add_child(std::make_unique<ui_text_label>(3, text_y, "Other:"));
	
	// Create the textbox, and hook it up so that when it gains focus, it auto-selects the "Other" option 
	// (or just rely on the fact that if a custom string is entered, it overrides).
	dlg->add_child(std::make_unique<ui_textbox>("custom_answer", 10, text_y, width - 14, ""));

	int current_y = text_y + 2;
	int btn_x_center = width / 2;
	
	dlg->add_child(std::make_unique<ui_button>("btn_ok", btn_x_center - 12, current_y, "   OK   ", 'O', [d = dlg.get()]() {
		auto custom = d->get_value("custom_answer");
		if (custom && !custom->empty()) {
			d->set_result(*custom);
		} else {
			auto opt = d->get_value("options");
			if (opt) d->set_result(*opt);
		}
		d->set_action(dialog_result::confirmed);
	}));
	
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", btn_x_center + 2, current_y, " Cancel ", 'C', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));

	dlg->set_focus_by_name("options");
	return dlg;
}


search_params extract_search_params(const dialog& dlg, const search_params& initial_params)
{
	search_params params = initial_params;
	
	auto q = dlg.get_value("query");
	if (q) params.query = *q;
	
	auto r = dlg.get_value("replacement");
	if (r) params.replacement = *r;
	
	auto ic = dlg.get_value("ignore_case");
	if (ic) params.ignore_case = (*ic == "false"); // Checkbox is "Case sensitive", so true means ignore_case = false
	
	auto ww = dlg.get_value("whole_words");
	if (ww) params.whole_words = (*ww == "true");
	
	auto re = dlg.get_value("regex");
	if (re) params.regex = (*re == "true");
	
	auto pr = dlg.get_value("prompt_on_replace");
	if (pr) params.prompt_on_replace = (*pr == "true");
	
	auto dir = dlg.get_value("direction");
	if (dir) params.backward = (*dir == "dir_backward");
	
	auto scope = dlg.get_value("scope");
	if (scope) params.selected_text_only = (*scope == "scope_selected");
	
	auto origin = dlg.get_value("origin");
	if (origin) params.from_cursor = (*origin == "origin_cursor");
	
	return params;
}

std::unique_ptr<dialog> create_search_dialog(const std::string &title, const search_params &initial_params, bool is_replace)
{
	int height = is_replace ? 18 : 16;
	auto dlg = std::make_unique<dialog>(title, 64, height);
	
	int y_off = is_replace ? 2 : 0;
	
	// Query
	dlg->add_child(std::make_unique<ui_text_label>(2, 2, "Text to find"));
	dlg->add_child(std::make_unique<ui_textbox>("query", 16, 2, 40, initial_params.query, [d = dlg.get()](const std::string&) {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	
	// Replace
	if (is_replace) {
		dlg->add_child(std::make_unique<ui_text_label>(2, 4, "Replace with"));
		dlg->add_child(std::make_unique<ui_textbox>("replacement", 16, 4, 40, initial_params.replacement, [d = dlg.get()](const std::string&) {
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
		opt_group->add_child(std::make_unique<ui_checkbox>("prompt_on_replace", 2, 4, "Prompt on replace", 'p', initial_params.prompt_on_replace));
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
	scope_radio->add_child(std::make_unique<ui_radio_choice>("scope_selected", 2, 2, "Selected text", 's', initial_params.selected_text_only));
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
		{"LLVM", 'L'}, {"Google", 'G'}, {"Chromium", 'C'}, {"Mozilla", 'M'},
		{"WebKit", 'W'}, {"Microsoft", 's'}, {"GNU", 'N'}, {".clang-format file", 'f'}
	};

	std::string current_style = config_manager::get_instance().get_clang_format_style();
	for (size_t i = 0; i < style_labels.size(); ++i) {
		bool selected = (current_style == style_labels[i].first);
		style_radio->add_child(std::make_unique<ui_radio_choice>(style_labels[i].first, 2, 1 + i, style_labels[i].first, style_labels[i].second, selected));
	}
	style_group->add_child(std::move(style_radio));
	dlg->add_child(std::move(style_group));

	// Build System group
	auto build_group = std::make_unique<ui_group_box>("build_group", 36, 2, 20, 5, " Build System ");
	auto build_radio = std::make_unique<ui_radiobutton_group>("build_system", 0, 0, 20, 5);

	std::vector<std::pair<std::string, char>> system_labels = {
		{"meson", 'm'}, {"cmake", 'k'}, {"make", 'a'}
	};

	std::string current_system = config_manager::get_instance().get_build_system();
	for (size_t i = 0; i < system_labels.size(); ++i) {
		bool selected = (current_system == system_labels[i].first);
		build_radio->add_child(std::make_unique<ui_radio_choice>(system_labels[i].first, 2, 1 + i, system_labels[i].first, system_labels[i].second, selected));
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
	dlg->add_child(std::make_unique<ui_checkbox>("lsp_enabled", 4, 16, "Enable LSP (clangd)", 'E', config_manager::get_instance().is_lsp_enabled()));
	dlg->add_child(std::make_unique<ui_checkbox>("auto_open_error", 4, 17, "Auto-open files for build errors", 'u', config_manager::get_instance().is_auto_open_error_files()));
	dlg->add_child(std::make_unique<ui_checkbox>("compile_on_save", 4, 18, "Compile f[i]le on save", 'i', config_manager::get_instance().is_compile_on_save()));
	dlg->add_child(std::make_unique<ui_checkbox>("log_all_tools", 4, 19, "Log all agent tool calls (debug)", 'g', config_manager::get_instance().is_log_all_tool_calls()));

	// Buttons
	dlg->add_child(std::make_unique<ui_button>("btn_ok", 10, 22, "  OK  ", 'o', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_cancel", 25, 22, " Cancel ", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}));
	dlg->add_child(std::make_unique<ui_button>("btn_help", 40, 22, " Help ", 'h', [d = dlg.get()]() {
		// Not currently hooked up to help system inside dialog, but returning help result
		d->set_action(dialog_result::confirmed);
		d->set_result("help");
	}));

	dlg->set_focus_by_name("style_group");

	return dlg;
}

void apply_settings_from_dialog(const dialog& dlg)
{
	auto& cfg = config_manager::get_instance();

	auto style = dlg.get_value("style");
	if (style) cfg.set_clang_format_style(*style);

	auto build_sys = dlg.get_value("build_system");
	if (build_sys) cfg.set_build_system(*build_sys);

	auto b_dir = dlg.get_value("build_dir");
	if (b_dir) cfg.set_build_directory(*b_dir);

	auto def_model = dlg.get_value("default_model_id");
	if (def_model) cfg.set_default_model_id(*def_model);

	auto lsp = dlg.get_value("lsp_enabled");
	if (lsp) cfg.set_lsp_enabled(*lsp == "true");

	auto auto_op = dlg.get_value("auto_open_error");
	if (auto_op) cfg.set_auto_open_error_files(*auto_op == "true");

	auto cmp = dlg.get_value("compile_on_save");
	if (cmp) cfg.set_compile_on_save(*cmp == "true");

	auto log_tools = dlg.get_value("log_all_tools");
	if (log_tools) cfg.set_log_all_tool_calls(*log_tools == "true");

	cfg.save();
}
// Ensure the #include "config_manager.h" is not duplicated if it's already there


class file_dialog_impl : public dialog
{
public:
	file_dialog_impl(const std::string &title, const std::string &initial_path)
		: dialog(title, 68, 17), initial_path_(initial_path)
	{
		// Name Label
		add_child(std::make_unique<ui_text_label>(2, 2, "Name"));
		
				auto on_tb_submit = [this](const std::string& val) {
			std::string final_val = val;
			std::string suggestion = get_fs_view()->get_autocomplete_suggestion(val);
			if (!suggestion.empty()) {
				final_val = suggestion;
			}
			if (final_val.empty()) return;
			
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
		tb->set_autocomplete_provider([this](const std::string& buf) {
			return get_fs_view()->get_autocomplete_suggestion(buf);
		});
		add_child(std::move(tb));

		// Files Label
		add_child(std::make_unique<ui_text_label>(2, 4, "Files"));

		// Filesystem View
		auto on_fs_change = [this](const std::string& name) {
			get_textbox()->set_buffer(name);
		};
		auto on_fs_submit = [this](const std::string& name) {
			set_action(dialog_result::confirmed);
			set_result((get_fs_view()->get_current_path() / name).string());
		};
		
		add_child(std::make_unique<ui_fileselector>("fileselector", 2, 5, 46, 8, initial_path, on_fs_change, on_fs_submit));
		auto fs_view_ptr = static_cast<ui_fileselector*>(children_.back().get());
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
	ui_fileselector* get_fs_view() const {
		for (auto& c : children_) {
			if (c->name() == "fileselector") return static_cast<ui_fileselector*>(c.get());
		}
		return nullptr;
	}
	ui_textbox* get_textbox() const {
		for (auto& c : children_) {
			if (c->name() == "filename") return static_cast<ui_textbox*>(c.get());
		}
		return nullptr;
	}

	std::string initial_path_;
};

std::unique_ptr<dialog> create_file_dialog(const std::string &title, const std::string &initial_path)
{
	return std::make_unique<file_dialog_impl>(title, initial_path);
}
