/* IMPORTANT:
 * Standard pattern for dialog boxes is to set their size automatically after a ->flow call:
 *	dlg->flow();
 *	dlg->set_width(flow_ptr->width());
 *	dlg->set_height(flow_ptr->height());
 */

#include "ui/dialog_factories.h"
#include "a2a/a2a_server.h"
#include "config_manager.h"
#include "project_manager.h"
#include "syntax_color_manager.h"

#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_color_picker.h"
#include "ui/components/ui_dropdown.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_tabbed_container.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"

#include <memory>
#include <string>
#include <vector>

std::unique_ptr<dialog> create_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Preferences", 60, 25);

	auto flow = std::make_unique<ui_vertical_flow>("settings_flow", 0, 0, 2, 1);

	// Clang Format Style group
	auto style_group = std::make_unique<ui_group_box>("style_group", 30, " Clang Format Style ");
	auto style_radio = std::make_unique<ui_radiobutton_group>("style");

	std::vector<std::pair<std::string, char>> style_labels = {{"LLVM", 'L'},    {"Google", 'G'},	   {"Chromium", 'C'},
								  {"Mozilla", 'M'}, {"WebKit", 'W'},	   {"Microsoft", 's'},
								  {"GNU", 'N'},	    {"Linux Kernel", 'K'}, {".clang-format file", 'f'}};

	std::string current_style = config_manager::get_instance().get_clang_format_style();
	for (size_t i = 0; i < style_labels.size(); ++i) {
		bool selected = (current_style == style_labels[i].first);
		style_radio->add_child(
		    std::make_unique<ui_radio_choice>(style_labels[i].first, style_labels[i].first, style_labels[i].second, selected));
	}
	style_group->add_child(std::move(style_radio));

	// Build System group
	auto build_group = std::make_unique<ui_group_box>("build_group", 24, " Build System ");
	auto build_radio = std::make_unique<ui_radiobutton_group>("build_system");

	std::vector<std::pair<std::string, char>> system_labels = {{"meson", 'm'}, {"cmake", 'k'}, {"make", 'a'}};

	std::string current_system = config_manager::get_instance().get_build_system();
	for (size_t i = 0; i < system_labels.size(); ++i) {
		bool selected = (current_system == system_labels[i].first);
		build_radio->add_child(
		    std::make_unique<ui_radio_choice>(system_labels[i].first, system_labels[i].first, system_labels[i].second, selected));
	}
	build_group->add_child(std::move(build_radio));

	// Row 1 (Style & Build Groups)
	auto row1 = std::make_unique<ui_horizontal_flow>("row1", 0, 0, 0, 0);
	row1->add_child(std::move(style_group));
	row1->add_child(std::move(build_group));
	flow->add_child(std::move(row1));

	// Build Directory and Model ID Inputs placed side-by-side to save vertical space.
	auto textboxes_row = std::make_unique<ui_horizontal_flow>("textboxes_row", 0, 0, 0, 0);
	textboxes_row->add_child(
	    std::make_unique<ui_textbox>("build_dir", 26, config_manager::get_instance().get_build_directory(), nullptr, "Build Dir: "));
	textboxes_row->add_child(std::make_unique<ui_textbox>("default_model_id", 28, config_manager::get_instance().get_default_model_id(),
							      nullptr, "Model ID:  "));
	flow->add_child(std::move(textboxes_row));

	// Primary Language and Standard Inputs placed side-by-side.
	auto lang_row = std::make_unique<ui_horizontal_flow>("lang_row", 0, 0, 0, 0);
	lang_row->add_child(
	    std::make_unique<ui_textbox>("primary_language", 26, config_manager::get_instance().get_primary_language(), nullptr, "Language:  "));
	lang_row->add_child(std::make_unique<ui_textbox>("primary_language_version", 28, config_manager::get_instance().get_primary_language_version(),
							      nullptr, "Standard:  "));
	flow->add_child(std::move(lang_row));

	// A2A Server security settings row
	auto a2a_sec_row = std::make_unique<ui_horizontal_flow>("a2a_sec_row", 0, 0, 0, 0);
	a2a_sec_row->add_child(std::make_unique<ui_textbox>("a2a_server_token", 30,
		config_manager::get_instance().get_a2a_server_token(), nullptr, "A2A Token:  "));
	auto a2a_grp = std::make_unique<ui_checkbox_group>("a2a_grp");
	a2a_grp->add_child(std::make_unique<ui_checkbox>("a2a_enforce_token", "Enforce A2A Token", 'T',
		config_manager::get_instance().is_a2a_server_token_enforced()));
	a2a_sec_row->add_child(std::move(a2a_grp));
	flow->add_child(std::move(a2a_sec_row));

	// Toggles split into two columns (side-by-side checkbox groups) to optimize layout height.
	auto toggles_row = std::make_unique<ui_horizontal_flow>("toggles_row", 0, 0, 0, 0);

	auto col1 = std::make_unique<ui_checkbox_group>("col1");
	col1->add_child(
	    std::make_unique<ui_checkbox>("lsp_enabled", "Enable LSP (clangd)", 'E', config_manager::get_instance().is_lsp_enabled()));
	col1->add_child(std::make_unique<ui_checkbox>("auto_open_error", "Auto-open build errors", 'u',
						      config_manager::get_instance().is_auto_open_error_files()));
	col1->add_child(std::make_unique<ui_checkbox>("compile_on_save", "Compile file on save", 'i',
						      config_manager::get_instance().is_compile_on_save()));

	auto col2 = std::make_unique<ui_checkbox_group>("col2");
	col2->add_child(std::make_unique<ui_checkbox>("log_all_tools", "Log agent tool calls", 'g',
						      config_manager::get_instance().is_log_all_tool_calls()));
	col2->add_child(std::make_unique<ui_checkbox>("shell_display_access", "Shell display access", 'd',
						      config_manager::get_instance().is_shell_display_access()));

	toggles_row->add_child(std::move(col1));
	toggles_row->add_child(std::move(col2));
	flow->add_child(std::move(toggles_row));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, 0, 0, 0);
	btns->set_centered(true);
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

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name(current_style.empty() ? "LLVM" : current_style);

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

	auto prim_lang = dlg.get_value("primary_language");
	if (prim_lang)
		cfg.set_primary_language(*prim_lang);

	auto prim_ver = dlg.get_value("primary_language_version");
	if (prim_ver)
		cfg.set_primary_language_version(*prim_ver);

	auto a2a_tok = dlg.get_value("a2a_server_token");
	if (a2a_tok) {
		cfg.set_a2a_server_token(*a2a_tok);
		a2a::a2a_server::get_instance().set_auth_token(*a2a_tok);
	}

	auto a2a_enf = dlg.get_value("a2a_enforce_token");
	if (a2a_enf) {
		bool enf = (*a2a_enf == "true");
		cfg.set_a2a_server_token_enforced(enf);
		a2a::a2a_server::get_instance().set_enforce_token(enf);
	}

	auto a2a_port_str = dlg.get_value("a2a_server_port");
	if (a2a_port_str) {
		try {
			int p = std::stoi(*a2a_port_str);
			if (p > 0 && p < 65536) {
				cfg.set_a2a_server_port(p);
			}
		} catch (...) {}
	}

	cfg.save_turboserver_config();

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

	auto log_shell = dlg.get_value("log_shell_commands");
	if (log_shell)
		cfg.set_log_shell_commands(*log_shell == "true");

	auto shell_display = dlg.get_value("shell_display_access");
	if (shell_display)
		cfg.set_shell_display_access(*shell_display == "true");

	auto paranoid = dlg.get_value("paranoid_mode");
	if (paranoid)
		cfg.set_paranoid_mode(*paranoid == "true");

	auto run_outside = dlg.get_value("run_outside_sandbox");
	if (run_outside)
		cfg.set_run_outside_sandbox(*run_outside == "true");

	auto net = dlg.get_value("allow_code_execution_network");
	if (net)
		cfg.set_allow_code_execution_network(*net == "true");
}

std::unique_ptr<dialog> create_editor_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Editor & Workspace Settings", 68, 22);
	auto main_flow = std::make_unique<ui_vertical_flow>("main_flow", 0, 0, 1, 1);

	auto tabbed = std::make_unique<ui_tabbed_container>("editor_tabbed", 0, 0, 64, 16);

	// --- Tab 1: General & Code ---
	auto tab1_flow = std::make_unique<ui_vertical_flow>("tab1_flow", 0, 0, 1, 1);
	auto style_group = std::make_unique<ui_group_box>("style_group", 38, " Clang Format Style ");
	auto style_radio = std::make_unique<ui_radiobutton_group>("style");
	std::vector<std::pair<std::string, char>> style_labels = {{"LLVM", 'L'},    {"Google", 'G'},	   {"Chromium", 'C'},
								  {"Mozilla", 'M'}, {"WebKit", 'W'},	   {"Microsoft", 's'},
								  {"GNU", 'N'},	    {"Linux Kernel", 'K'}, {".clang-format file", 'f'}};
	std::string current_style = config_manager::get_instance().get_clang_format_style();
	for (const auto &pair : style_labels) {
		bool selected = (current_style == pair.first);
		style_radio->add_child(std::make_unique<ui_radio_choice>(pair.first, pair.first, pair.second, selected));
	}
	style_group->add_child(std::move(style_radio));
	tab1_flow->add_child(std::move(style_group));

	auto tab1_chk = std::make_unique<ui_checkbox_group>("tab1_chk");
	tab1_chk->add_child(std::make_unique<ui_checkbox>("compile_on_save", "Compile file on save", 'i',
							  config_manager::get_instance().is_compile_on_save()));
	tab1_flow->add_child(std::move(tab1_chk));
	tabbed->add_tab_page("general", "General & Code", std::move(tab1_flow));

	// --- Tab 2: Build & Run ---
	auto tab2_flow = std::make_unique<ui_vertical_flow>("tab2_flow", 0, 0, 1, 1);
	auto build_group = std::make_unique<ui_group_box>("build_group", 38, " Build System ");
	auto build_radio = std::make_unique<ui_radiobutton_group>("build_system");
	std::vector<std::pair<std::string, char>> system_labels = {{"meson", 'm'}, {"cmake", 'k'}, {"make", 'a'}};
	std::string current_system = config_manager::get_instance().get_build_system();
	for (const auto &pair : system_labels) {
		bool selected = (current_system == pair.first);
		build_radio->add_child(std::make_unique<ui_radio_choice>(pair.first, pair.first, pair.second, selected));
	}
	build_group->add_child(std::move(build_radio));
	tab2_flow->add_child(std::move(build_group));

	tab2_flow->add_child(std::make_unique<ui_textbox>("build_dir", 26, config_manager::get_instance().get_build_directory(), nullptr, "Build Dir: "));
	tab2_flow->add_child(std::make_unique<ui_textbox>("main_executable", 26, config_manager::get_instance().get_main_executable(), nullptr, "Exe Path:  "));
	tab2_flow->add_child(std::make_unique<ui_textbox>("run_arguments", 26, config_manager::get_instance().get_run_arguments(), nullptr, "Run Args:  "));
	tabbed->add_tab_page("build_run", "Build & Run", std::move(tab2_flow));

	// --- Tab 3: LSP & Features ---
	auto tab3_flow = std::make_unique<ui_vertical_flow>("tab3_flow", 0, 0, 1, 1);
	auto lsp_grp = std::make_unique<ui_checkbox_group>("lsp_grp");
	lsp_grp->add_child(std::make_unique<ui_checkbox>("lsp_enabled", "Enable LSP (clangd)", 'E', config_manager::get_instance().is_lsp_enabled()));
	lsp_grp->add_child(std::make_unique<ui_checkbox>("auto_open_error", "Auto-open build errors", 'u', config_manager::get_instance().is_auto_open_error_files()));
	lsp_grp->add_child(std::make_unique<ui_checkbox>("shell_display_access", "Shell display access", 'd', config_manager::get_instance().is_shell_display_access()));
	lsp_grp->add_child(std::make_unique<ui_checkbox>("log_shell_commands", "Log shell commands", 'h', config_manager::get_instance().is_log_shell_commands()));
	tab3_flow->add_child(std::move(lsp_grp));
	tabbed->add_tab_page("lsp", "LSP & Features", std::move(tab3_flow));

	// --- Tab 4: Syntax Colors ---
	auto tab4_flow = std::make_unique<ui_vertical_flow>("tab4_flow", 0, 0, 1, 1);
	tab4_flow->add_child(std::make_unique<ui_text_label>("Syntax Highlight Colors:"));

	static const std::vector<syntax_attribute> attrs = {
		syntax_attribute::normal, syntax_attribute::keyword, syntax_attribute::comment,
		syntax_attribute::string_literal, syntax_attribute::heading, syntax_attribute::bold,
		syntax_attribute::italic, syntax_attribute::list_item, syntax_attribute::trailing_space
	};
	std::vector<std::string> item_names;
	for (auto attr : attrs) {
		item_names.push_back(syntax_color_manager::get_attribute_name(attr));
	}
	auto [init_fg, init_bg] = syntax_color_manager::get_instance().get_color(attrs[0]);
	auto listbox_holder = std::make_shared<ui_listbox*>(nullptr);

	auto picker = std::make_unique<ui_color_picker>("color_picker", 22, 1, init_fg, init_bg, 7, [listbox_holder](uint8_t fg, uint8_t bg) {
		ui_listbox *listbox_ptr = *listbox_holder;
		if (listbox_ptr) {
			int idx = listbox_ptr->get_selected_index();
			if (idx >= 0 && idx < static_cast<int>(attrs.size())) {
				syntax_color_manager::get_instance().set_color(attrs[idx], fg, bg);
			}
		}
	});
	auto picker_raw = picker.get();

	auto listbox = std::make_unique<ui_listbox>("attribute_list", 0, 0, 18, 8, [picker_raw](int idx) {
		if (idx >= 0 && idx < static_cast<int>(attrs.size())) {
			auto [fg, bg] = syntax_color_manager::get_instance().get_color(attrs[idx]);
			picker_raw->set_selected_colors(fg, bg);
		}
	}, nullptr);
	*listbox_holder = listbox.get();
	listbox->set_items(item_names);
	listbox->set_selected_index(0);

	auto syntax_row = std::make_unique<ui_horizontal_flow>("syntax_row", 0, 0);
	syntax_row->add_child(std::move(listbox));
	syntax_row->add_child(std::move(picker));
	tab4_flow->add_child(std::move(syntax_row));
	tabbed->add_tab_page("syntax", "Syntax Colors", std::move(tab4_flow));

	main_flow->add_child(std::move(tabbed));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons", 0, 0, 0, 0);
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK (Save Project)", 'O', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_global", "Save Global", 'v', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("save_global");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_cancel", "Cancel", 'C', [d = dlg.get()]() {
		d->set_result("cancel");
		d->set_action(dialog_result::cancelled);
	}, true));
	main_flow->add_child(std::move(btns));

	auto flow_ptr = main_flow.get();
	dlg->add_child(std::move(main_flow));
	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	return dlg;
}

void apply_editor_settings_from_dialog(const dialog &dlg)
{
	apply_settings_from_dialog(dlg);
}

std::unique_ptr<dialog> create_run_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("Run Settings", 60, 15);

	auto flow = std::make_unique<ui_vertical_flow>("run_settings_flow", 2, 1);

	// Main Executable Input
	auto exe_row = std::make_unique<ui_horizontal_flow>("exe_row", 0, 0);
	exe_row->add_child(std::make_unique<ui_text_label>("Main Executable:"));
	auto candidates = project_manager::get_instance().detect_executable_candidates();
	exe_row->add_child(
	    std::make_unique<ui_dropdown>("main_executable", 0, 0, 35, config_manager::get_instance().get_main_executable(), candidates));
	flow->add_child(std::move(exe_row));

	// Arguments Input
	auto args_row = std::make_unique<ui_horizontal_flow>("args_row", 0, 0);
	args_row->add_child(std::make_unique<ui_text_label>("Arguments:      "));
	args_row->add_child(std::make_unique<ui_textbox>("run_arguments", 35, config_manager::get_instance().get_run_arguments()));
	flow->add_child(std::move(args_row));

	// Run Target Mode group
	auto mode_group = std::make_unique<ui_group_box>("mode_group", 52, " Run Target Mode ");
	auto mode_radio = std::make_unique<ui_radiobutton_group>("run_target_mode");

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
		mode_radio->add_child(
		    std::make_unique<ui_radio_choice>(mode_options[i].value, mode_options[i].label, mode_options[i].hotkey, selected));
	}
	mode_group->add_child(std::move(mode_radio));
	flow->add_child(std::move(mode_group));

	// Auto-start debugger checkbox
	bool auto_start = config_manager::get_instance().get_gdb_auto_continue();
	auto auto_start_group = std::make_unique<ui_checkbox_group>("auto_start_group");
	auto_start_group->add_child(
	    std::make_unique<ui_checkbox>("gdb_auto_continue", "Auto-start the application on debugger startup", 'a', auto_start));
	flow->add_child(std::move(auto_start_group));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
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
	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

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

std::unique_ptr<dialog> create_syntax_colors_dialog()
{
	auto dlg = std::make_unique<dialog>("Syntax Highlight Colors", 78, 14);

	static const std::vector<syntax_attribute> attrs = {
		syntax_attribute::normal,
		syntax_attribute::keyword,
		syntax_attribute::comment,
		syntax_attribute::string_literal,
		syntax_attribute::heading,
		syntax_attribute::bold,
		syntax_attribute::italic,
		syntax_attribute::list_item,
		syntax_attribute::trailing_space
	};

	std::vector<std::string> item_names;
	for (auto attr : attrs) {
		item_names.push_back(syntax_color_manager::get_attribute_name(attr));
	}
	auto [init_fg, init_bg] = syntax_color_manager::get_instance().get_color(attrs[0]);

	auto listbox_holder = std::make_shared<ui_listbox*>(nullptr);

	auto picker = std::make_unique<ui_color_picker>("color_picker", 26, 1, init_fg, init_bg, 7, [listbox_holder](uint8_t fg, uint8_t bg) {
		ui_listbox *listbox_ptr = *listbox_holder;
		if (listbox_ptr) {
			int idx = listbox_ptr->get_selected_index();
			if (idx >= 0 && idx < static_cast<int>(attrs.size())) {
				syntax_color_manager::get_instance().set_color(attrs[idx], fg, bg);
			}
		}
	});
	auto picker_raw = picker.get();

	auto listbox = std::make_unique<ui_listbox>("attribute_list", 2, 1, 22, 10, [picker_raw](int idx) {
		if (idx >= 0 && idx < static_cast<int>(attrs.size())) {
			auto [fg, bg] = syntax_color_manager::get_instance().get_color(attrs[idx]);
			picker_raw->set_selected_colors(fg, bg);
		}
	}, nullptr);
	*listbox_holder = listbox.get();
	ui_listbox *listbox_ptr = listbox.get();

	listbox->set_items(item_names);
	listbox->set_selected_index(0);

	dlg->add_child(std::move(listbox));
	dlg->add_child(std::move(picker));

	auto buttons = std::make_unique<ui_buttons_horizontal>("buttons", 2, 12, 74, 1);
	buttons->set_centered(true);
	
	buttons->add_child(std::make_unique<ui_button>("btn_ok", "OK (Save)", 'O', [d = dlg.get()]() {
		syntax_color_manager::get_instance().save();
		d->set_action(dialog_result::confirmed);
		d->set_result("ok");
	}));

	buttons->add_child(std::make_unique<ui_button>("btn_cancel", "Cancel", 'C', [d = dlg.get()]() {
		syntax_color_manager::get_instance().reload();
		d->set_action(dialog_result::cancelled);
		d->set_result("cancel");
	}, true));

	dlg->add_child(std::move(buttons));

	dlg->set_focused_child(listbox_ptr);
	listbox_ptr->set_focus(true);

	return dlg;
}
