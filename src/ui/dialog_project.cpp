/* IMPORTANT:
 * Standard pattern for dialog boxes is to set their size automatically after a ->flow call:
 *	dlg->flow();
 *	dlg->set_width(flow_ptr->width());
 *	dlg->set_height(flow_ptr->height());
 */

#include "ui/dialog_factories.h"
#include "codereview_manager.h"
#include "config_manager.h"
#include "fs_utils.h"
#include "images/image_manager.h"
#include "ncurses.h"
#include "project_manager.h"
#include "project_template_manager.h"

#include "ui/components/ui_button.h"
#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_buttons_vertical.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_dropdown.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_fileselector.h"
#include "ui/components/ui_group_box.h"
#include "ui/components/ui_horizontal_flow.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_multiline_edit.h"
#include "ui/components/ui_paged_container.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_thumbnail.h"
#include "ui/components/ui_vertical_flow.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class file_dialog_impl : public dialog
{
      public:
	file_dialog_impl(const std::string &title, const std::string &initial_path) : dialog(title, 68, 17), initial_path_(initial_path)
	{
		auto on_tb_submit = [this](const std::string &val) {
			std::string final_val = val;
			if (!val.contains('/') && !fs::path(val).is_absolute()) {
				std::string suggestion = get_fs_view()->get_autocomplete_suggestion(val);
				if (!suggestion.empty()) {
					final_val = suggestion;
				}
			}
			if (final_val.empty())
				return;

			fs::path entered_path = fs::path(final_val).is_absolute() ? fs::path(final_val) : (get_fs_view()->get_current_path() / final_val);
			if (fs::exists(entered_path) && fs::is_directory(entered_path)) {
				get_fs_view()->set_current_path(fs::canonical(entered_path));
				get_textbox()->set_buffer("");
			} else {
				set_action(dialog_result::confirmed);
				set_result(entered_path.string());
			}
		};

		auto on_fs_change = [this](const std::string &name) { get_textbox()->set_buffer(name); };
		auto on_fs_submit = [this](const std::string &name) {
			set_action(dialog_result::confirmed);
			set_result((get_fs_view()->get_current_path() / name).string());
		};

		auto flow = std::make_unique<ui_vertical_flow>("file_dialog_flow", 2, 1);

		auto top_section = std::make_unique<ui_horizontal_flow>("top_section", 0, 0);

		auto left_col = std::make_unique<ui_vertical_flow>("left_col", 0, 0);

		auto name_row = std::make_unique<ui_horizontal_flow>("name_row", 0, 0);
		name_row->add_child(std::make_unique<ui_text_label>("Name"));
		auto tb = std::make_unique<ui_textbox>("filename", 0, 0, 40, "", on_tb_submit);
		tb->set_history_enabled(true, "open_filename");
		tb->set_autocomplete_provider([this](const std::string &buf) { return get_fs_view()->get_autocomplete_suggestion(buf); });
		tb_ = tb.get();
		name_row->add_child(std::move(tb));
		left_col->add_child(std::move(name_row));

		left_col->add_child(std::make_unique<ui_text_label>("Files"));

		auto fs_view = std::make_unique<ui_fileselector>("fileselector", 0, 0, 46, 8, initial_path, on_fs_change, on_fs_submit);
		fs_view_ = fs_view.get();
		left_col->add_child(std::move(fs_view));

		top_section->add_child(std::move(left_col));

		auto btns = std::make_unique<ui_buttons_vertical>("buttons", 0, 0, 12, 8);
		btns->add_child(std::make_unique<ui_button>("btn_ok", "Ok", 'o', [this]() {
			std::string val = *get_value("filename");
			if (!val.empty()) {
				fs::path entered_path = fs::path(val).is_absolute() ? fs::path(val) : (get_fs_view()->get_current_path() / val);
				if (fs::exists(entered_path) && fs::is_directory(entered_path)) {
					get_fs_view()->set_current_path(fs::canonical(entered_path));
					get_textbox()->set_buffer("");
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
		top_section->add_child(std::move(btns));

		flow->add_child(std::move(top_section));

		auto info_panel = std::make_unique<ui_file_info_panel>(0, 0, 66, fs_view_);
		flow->add_child(std::move(info_panel));

		auto flow_ptr = flow.get();
		add_child(std::move(flow));

		this->flow();
		set_width(flow_ptr->width());
		set_height(flow_ptr->height());

		set_focus_by_name("filename");
		get_textbox()->set_buffer("");
	}

      private:
	ui_fileselector *get_fs_view() const
	{
		return fs_view_;
	}
	ui_textbox *get_textbox() const
	{
		return tb_;
	}

	std::string initial_path_;
	ui_fileselector *fs_view_{nullptr};
	ui_textbox *tb_{nullptr};
};

std::unique_ptr<dialog> create_file_dialog(const std::string &title, const std::string &initial_path)
{
	return std::make_unique<file_dialog_impl>(title, initial_path);
}

std::unique_ptr<dialog> create_code_review_edit_dialog(const review_item &item)
{
	// Dialog size is auto-set via flow container dimensions below
	auto dlg = std::make_unique<dialog>("Edit Code Review Item", 60, 20);

	auto flow = std::make_unique<ui_vertical_flow>("edit_flow", 2, 1, 1);

	flow->add_child(std::make_unique<ui_text_label>("Summary:"));
	auto summary_box = std::make_unique<ui_textbox>("summary", 60, item.summary);
	flow->add_child(std::move(summary_box));

	flow->add_child(std::make_unique<ui_text_label>("Description:"));
	auto desc_box = std::make_unique<ui_multiline_edit>("description", 60, 4, nullptr);
	desc_box->set_buffer(item.description);
	flow->add_child(std::move(desc_box));

	flow->add_child(std::make_unique<ui_text_label>("Proposed Fix:"));
	auto fix_box = std::make_unique<ui_multiline_edit>("proposed_fix", 60, 4, nullptr);
	fix_box->set_buffer(item.proposed_fix);
	flow->add_child(std::move(fix_box));

	// Dropdowns for Severity and State in a single row
	auto dropdowns_row = std::make_unique<ui_horizontal_flow>("dropdowns_row", 0, 0);
	dropdowns_row->add_child(std::make_unique<ui_text_label>("Severity: "));
	std::vector<std::string> severities = {"nit", "low", "medium", "high", "critical"};
	dropdowns_row->add_child(std::make_unique<ui_dropdown>("severity", 12, item.severity, severities));

	dropdowns_row->add_child(std::make_unique<ui_text_label>("  State: "));
	std::vector<std::string> states = {"invalid", "new", "confirmed", "disputed", "stale", "resolved", "verified-fixed"};
	dropdowns_row->add_child(std::make_unique<ui_dropdown>("state", 18, item.state, states));
	flow->add_child(std::move(dropdowns_row));

	// Action buttons
	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_ok", "OK", 'O', [d = dlg.get()]() {
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

	dlg->set_focus_by_name("summary");

	return dlg;
}

class image_manager_dialog_impl : public dialog
{
      public:
	image_manager_dialog_impl() : dialog("Image VFS Manager", 72, 21)
	{
		auto on_selection_changed = [this](int idx) {
			update_selection(idx);
		};

		auto on_submit = [this](int /*idx*/) {
			save_selected();
		};

		auto flow = std::make_unique<ui_vertical_flow>("main_flow", 2, 1, 0);

		auto top_section = std::make_unique<ui_horizontal_flow>("top_section", 0, 0);

		auto left_col = std::make_unique<ui_vertical_flow>("left_col", 0, 0, 0);
		left_col->add_child(std::make_unique<ui_text_label>("VFS Images:"));

		auto lb = std::make_unique<ui_listbox>("image_list", 40, 9, on_selection_changed, on_submit);
		lb_ = lb.get();
		left_col->add_child(std::move(lb));

		top_section->add_child(std::move(left_col));

		auto right_col = std::make_unique<ui_vertical_flow>("right_col", 0, 0, 0);
		right_col->add_child(std::make_unique<ui_text_label>("Image Info:"));

		auto lbl_alias = std::make_unique<ui_text_label>("Alias: -");
		lbl_alias_ = lbl_alias.get();
		right_col->add_child(std::move(lbl_alias));

		auto lbl_dims = std::make_unique<ui_text_label>("Dims:  -");
		lbl_dims_ = lbl_dims.get();
		right_col->add_child(std::move(lbl_dims));

		auto lbl_mime = std::make_unique<ui_text_label>("MIME:  -");
		lbl_mime_ = lbl_mime.get();
		right_col->add_child(std::move(lbl_mime));

		auto lbl_size = std::make_unique<ui_text_label>("Size:  -");
		lbl_size_ = lbl_size.get();
		right_col->add_child(std::move(lbl_size));

		right_col->add_child(std::make_unique<ui_text_label>(""));

		// Dynamic color thumbnail widget: width 20, height 5 (scaled up if terminal is large)
		int thumb_w = 20;
		int thumb_h = 5;
		int max_y = 0, max_x = 0;
		getmaxyx(stdscr, max_y, max_x);
		if (max_y >= 32) {
			thumb_w = 29;
			thumb_h = 8;
		}
		auto thumb = std::make_unique<ui_thumbnail>("thumbnail", 0, 0, thumb_w, thumb_h);
		thumb_ = thumb.get();
		right_col->add_child(std::move(thumb));

		top_section->add_child(std::move(right_col));
		flow->add_child(std::move(top_section));

		flow->add_child(std::make_unique<ui_text_label>(""));

		auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
		btns->set_centered(true);
		btns->add_child(std::make_unique<ui_button>("btn_import", "Import", 'i', [this]() {
			import_image();
		}));
		btns->add_child(std::make_unique<ui_button>("btn_save", "Save", 's', [this]() {
			save_selected();
		}));
		btns->add_child(std::make_unique<ui_button>("btn_delete", "Delete", 'd', [this]() {
			delete_selected();
		}));
		btns->add_child(std::make_unique<ui_button>("btn_close", "Close", 'c', [this]() {
			set_action(dialog_result::cancelled);
			set_result("cancel");
		}));

		flow->add_child(std::move(btns));
		auto flow_ptr = flow.get();
		add_child(std::move(flow));

		this->flow();
		set_width(flow_ptr->width());
		set_height(flow_ptr->height());

		refresh_list();
		populate_list();
		update_selection(0);

		set_focused_child(lb_);
		lb_->set_focus(true);
	}

	void refresh_list()
	{
		mappings_ = images::image_manager::get_instance().get_all_mappings();
	}

	void populate_list()
	{
		std::vector<std::string> items;
		for (const auto &meta : mappings_) {
			std::string name = meta.names.empty() ? meta.sha256.substr(0, 8) : meta.names[0];
			items.push_back(name);
		}
		lb_->set_items(items);
	}

	void update_selection(int idx)
	{
		if (idx >= 0 && idx < (int)mappings_.size()) {
			const auto &meta = mappings_[idx];
			std::string name = meta.names.empty() ? "(none)" : meta.names[0];
			lbl_alias_->set_text("Alias: " + name);
			lbl_dims_->set_text("Dims:  " + std::to_string(meta.width) + "x" + std::to_string(meta.height));
			lbl_mime_->set_text("MIME:  " + meta.mime_type);
			lbl_size_->set_text("Size:  " + std::to_string(meta.size_bytes) + " B");

			std::string uri = "images://by-sha256/" + meta.sha256;
			thumb_->set_image_path(uri);
		} else {
			lbl_alias_->set_text("Alias: -");
			lbl_dims_->set_text("Dims:  -");
			lbl_mime_->set_text("MIME:  -");
			lbl_size_->set_text("Size:  -");
			thumb_->clear_image();
		}
	}

	void import_image()
	{
		set_action(dialog_result::confirmed);
		set_result("import");
	}

	void save_selected()
	{
		int idx = lb_->get_selected_index();
		if (idx >= 0 && idx < (int)mappings_.size()) {
			set_action(dialog_result::confirmed);
			set_result("save:images://by-sha256/" + mappings_[idx].sha256);
		}
	}

	void delete_selected()
	{
		int idx = lb_->get_selected_index();
		if (idx >= 0 && idx < (int)mappings_.size()) {
			std::string uri = "images://by-sha256/" + mappings_[idx].sha256;
			images::image_manager::get_instance().delete_image(uri);
			refresh_list();
			populate_list();
			int new_idx = std::min(idx, (int)mappings_.size() - 1);
			if (new_idx >= 0) {
				lb_->set_selected_index(new_idx);
				update_selection(new_idx);
			} else {
				update_selection(-1);
			}
		}
	}

      private:
	ui_listbox *lb_{nullptr};
	ui_text_label *lbl_alias_{nullptr};
	ui_text_label *lbl_dims_{nullptr};
	ui_text_label *lbl_mime_{nullptr};
	ui_text_label *lbl_size_{nullptr};
	ui_thumbnail *thumb_{nullptr};

	std::vector<images::image_metadata> mappings_;
};

std::unique_ptr<dialog> create_image_manager_dialog()
{
	return std::make_unique<image_manager_dialog_impl>();
}

std::unique_ptr<dialog> create_new_project_dialog()
{
	auto dlg = std::make_unique<dialog>("Create New Project", 1, 1);

	std::string default_dir = project_manager::get_instance().get_project_root();
	std::string default_name = std::filesystem::path(default_dir).filename().string();
	if (default_name.empty() || default_name == "/") {
		default_name = "my_app";
	}

	auto wizard = std::make_unique<ui_paged_container>("wizard");
	auto *wizard_ptr = wizard.get();

	// Page 0: Project Identity & Location
	auto page0 = std::make_unique<ui_vertical_flow>("page0", 2, 1, 0);
	page0->add_child(std::make_unique<ui_text_label>("Step 1 of 3: Project Identity & Location"));
	page0->add_child(std::make_unique<ui_text_label>(""));

	auto row_name = std::make_unique<ui_horizontal_flow>("row_name", 0, 0);
	row_name->add_child(std::make_unique<ui_text_label>("Project Name:   "));
	row_name->add_child(std::make_unique<ui_textbox>("project_name", 30, default_name));
	page0->add_child(std::move(row_name));

	auto row_exec = std::make_unique<ui_horizontal_flow>("row_exec", 0, 0);
	row_exec->add_child(std::make_unique<ui_text_label>("Target Binary:  "));
	row_exec->add_child(std::make_unique<ui_textbox>("executable_name", 30, default_name));
	page0->add_child(std::move(row_exec));

	auto row_dir = std::make_unique<ui_horizontal_flow>("row_dir", 0, 0);
	row_dir->add_child(std::make_unique<ui_text_label>("Directory:      "));
	row_dir->add_child(std::make_unique<ui_textbox>("target_directory", 30, default_dir));
	page0->add_child(std::move(row_dir));

	wizard_ptr->add_page(std::move(page0));

	// Page 1: Select Programming Language
	auto page1 = std::make_unique<ui_vertical_flow>("page1", 2, 1, 0);
	page1->add_child(std::make_unique<ui_text_label>("Step 2 of 3: Select Programming Language"));
	page1->add_child(std::make_unique<ui_text_label>(""));

	auto lang_group = std::make_unique<ui_radiobutton_group>("language", false);
	lang_group->add_child(std::make_unique<ui_radio_choice>("C++", "C++", 'C', true));
	lang_group->add_child(std::make_unique<ui_radio_choice>("C", "C", 'a', false));
	lang_group->add_child(std::make_unique<ui_radio_choice>("Python", "Python", 'P', false));
	lang_group->add_child(std::make_unique<ui_radio_choice>("Rust", "Rust", 'R', false));
	lang_group->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'O', false));
	page1->add_child(std::move(lang_group));

	wizard_ptr->add_page(std::move(page1));

	// Page 2: Build System & Language Standard Options
	auto page2 = std::make_unique<ui_vertical_flow>("page2", 2, 1, 0);
	page2->add_child(std::make_unique<ui_text_label>("Step 3 of 3: Build System & Language Standard"));
	page2->add_child(std::make_unique<ui_text_label>(""));

	auto row_stack = std::make_unique<ui_horizontal_flow>("row_stack", 0, 0);

	auto build_box = std::make_unique<ui_group_box>("gb_build", 24, "Build System");
	auto build_group = std::make_unique<ui_radiobutton_group>("buildsystem", false);
	auto *build_group_ptr = build_group.get();
	build_box->add_child(std::move(build_group));
	row_stack->add_child(std::move(build_box));

	auto std_box = std::make_unique<ui_group_box>("gb_std", 24, "Language Standard");
	auto std_group = std::make_unique<ui_radiobutton_group>("language_standard", false);
	auto *std_group_ptr = std_group.get();
	std_box->add_child(std::move(std_group));
	row_stack->add_child(std::move(std_box));

	page2->add_child(std::move(row_stack));
	page2->add_child(std::make_unique<ui_text_label>(""));

	auto row_git = std::make_unique<ui_horizontal_flow>("row_git", 0, 0);
	row_git->add_child(std::make_unique<ui_checkbox>("init_git", "Initialize Git repository", 'g', true));
	page2->add_child(std::move(row_git));

	wizard_ptr->add_page(std::move(page2));

	// Page Entry Callback: Populate Page 2 based on Page 1 language selection
	wizard_ptr->set_page_entered_callback([wizard_ptr, build_group_ptr, std_group_ptr](size_t page) {
		if (page == 2 && build_group_ptr && std_group_ptr) {
			std::string selected_lang = wizard_ptr->get_value("language").value_or("C++");

			build_group_ptr->clear_children();
			std_group_ptr->clear_children();

			if (selected_lang == "C++") {
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Meson", "Meson", 'M', true));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("CMake", "CMake", 'K', false));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'O', false));

				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C++23", "C++23", '3', true));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C++20", "C++20", '0', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C++17", "C++17", '7', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'r', false));
			} else if (selected_lang == "C") {
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Meson", "Meson", 'M', true));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("CMake", "CMake", 'K', false));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'O', false));

				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C17", "C17", '7', true));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C11", "C11", '1', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("C99", "C99", '9', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'r', false));
			} else if (selected_lang == "Python") {
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("pyproject.toml", "pyproject.toml", 'p', true));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Meson", "Meson", 'M', false));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'O', false));

				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("3.11+", "3.11+", '1', true));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("3.10", "3.10", '0', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("3.9", "3.9", '9', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'r', false));
			} else if (selected_lang == "Rust") {
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Cargo", "Cargo", 'C', true));
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'O', false));

				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("2021 Edition", "2021 Edition", '1', true));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("2018 Edition", "2018 Edition", '8', false));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("Other", "Other", 'r', false));
			} else {
				// Other language
				build_group_ptr->add_child(std::make_unique<ui_radio_choice>("None / Custom", "None / Custom", 'N', true));
				std_group_ptr->add_child(std::make_unique<ui_radio_choice>("Custom / Default", "Custom / Default", 'D', true));
			}
			build_group_ptr->flow();
			std_group_ptr->flow();
		}
	});

	// Page Validation Callback: Ensure project name and directory are non-empty
	wizard_ptr->set_validate_page_callback([wizard_ptr](size_t page, std::string &out_err) {
		if (page == 0) {
			auto name = wizard_ptr->get_value("project_name").value_or("");
			auto dir = wizard_ptr->get_value("target_directory").value_or("");
			if (name.empty()) {
				out_err = "Project name cannot be empty.";
				return false;
			}
			if (dir.empty()) {
				out_err = "Target directory cannot be empty.";
				return false;
			}
		}
		return true;
	});

	dlg->add_child(std::move(wizard));

	dlg->flow();
	dlg->set_width(wizard_ptr->width() + 4);
	dlg->set_height(wizard_ptr->height() + 2);

	return dlg;
}

bool apply_new_project_from_dialog(const dialog &dlg, std::string &out_error)
{
	turbostar::project_create_options opts;
	opts.project_name = dlg.get_value("project_name").value_or("my_app");
	opts.executable_name = dlg.get_value("executable_name").value_or(opts.project_name);
	opts.language = dlg.get_value("language").value_or("C++");
	opts.buildsystem = dlg.get_value("buildsystem").value_or("Meson");
	opts.language_standard = dlg.get_value("language_standard").value_or("C++23");
	opts.target_directory = dlg.get_value("target_directory").value_or(project_manager::get_instance().get_project_root());
	opts.init_git = (dlg.get_value("init_git").value_or("true") == "true");

	bool ok = turbostar::project_template_manager::get_instance().create_project(opts, out_error);
	if (ok) {
		config_manager::get_instance().set_build_system(opts.buildsystem);
		config_manager::get_instance().set_primary_language(opts.language);
		config_manager::get_instance().set_primary_language_version(opts.language_standard);

		std::string cache_root = fs_utils::get_project_cache_root();
		if (!cache_root.empty()) {
			config_manager::get_instance().save_project(cache_root);
		} else {
			config_manager::get_instance().save_global();
		}
	}
	return ok;
}
