/* IMPORTANT:
 * Standard pattern for dialog boxes is to set their size automatically after a ->flow call:
 *	dlg->flow();
 *	dlg->set_width(flow_ptr->width());
 *	dlg->set_height(flow_ptr->height());
 */

#include "ui/dialog_factories.h"
#include "a2a/a2a_server.h"
#include "a2a/a2a_server_manager.h"
#include "ansi.h"
#include "config_manager.h"
#include "ncurses.h"

#include "ui/components/ui_buttons_horizontal.h"
#include "ui/components/ui_checkbox.h"
#include "ui/components/ui_checkbox_group.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_tabbed_container.h"
#include "ui/components/ui_text_label.h"
#include "ui/components/ui_textbox.h"
#include "ui/components/ui_vertical_flow.h"

#include <format>
#include <memory>
#include <string>
#include <vector>

std::unique_ptr<dialog> create_a2a_settings_dialog()
{
	auto dlg = std::make_unique<dialog>("A2A & Remote Settings", 68, 22);
	auto main_flow = std::make_unique<ui_vertical_flow>("main_flow", 0, 0, 1, 1);

	auto tabbed = std::make_unique<ui_tabbed_container>("a2a_tabbed", 0, 0, 64, 16);

	// --- Tab 1: Server Security ---
	auto tab1_flow = std::make_unique<ui_vertical_flow>("tab1_flow", 0, 0, 1, 1);
	tab1_flow->add_child(std::make_unique<ui_textbox>("a2a_server_token", 38,
		config_manager::get_instance().get_a2a_server_token(), nullptr, "A2A Token:  "));

	auto tok_btns = std::make_unique<ui_buttons_horizontal>("tok_btns");
	tok_btns->add_child(std::make_unique<ui_button>("btn_gen_tok", "Generate", 'G', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("gen_a2a_token");
	}));
	tok_btns->add_child(std::make_unique<ui_button>("btn_copy_tok", "Copy", 'C', [d = dlg.get()]() {
		auto tok = d->get_value("a2a_server_token");
		if (tok && !tok->empty()) {
			ansi::copy_to_clipboard(*tok);
		}
	}));
	tab1_flow->add_child(std::move(tok_btns));

	auto a2a_grp = std::make_unique<ui_checkbox_group>("a2a_grp");
	a2a_grp->add_child(std::make_unique<ui_checkbox>("a2a_enforce_token", "Enforce Bearer Token Authentication", 'T',
		config_manager::get_instance().is_a2a_server_token_enforced()));
	tab1_flow->add_child(std::move(a2a_grp));
	tabbed->add_tab_page("security", "Server Security", std::move(tab1_flow));

	// --- Tab 2: Server Runtime ---
	auto tab2_flow = std::make_unique<ui_vertical_flow>("tab2_flow", 0, 0, 1, 1);
	tab2_flow->add_child(std::make_unique<ui_textbox>("a2a_server_port", 26,
		std::to_string(config_manager::get_instance().get_a2a_server_port()), nullptr, "Server Port: "));
	auto runtime_grp = std::make_unique<ui_checkbox_group>("runtime_grp");
	runtime_grp->add_child(std::make_unique<ui_checkbox>("git_worktree_mode", "Pre-seed Task Workspaces with Git Worktrees", 'W', true));
	tab2_flow->add_child(std::move(runtime_grp));
	tabbed->add_tab_page("runtime", "Server Runtime", std::move(tab2_flow));

	// --- Tab 3: Remote Servers ---
	auto tab3_flow = std::make_unique<ui_vertical_flow>("tab3_flow", 0, 0, 1, 1);
	tab3_flow->add_child(std::make_unique<ui_text_label>("Registered Remote A2A Servers:"));
	auto remotes_list = std::make_unique<ui_listbox>("remote_list", 38, 5, nullptr, nullptr);
	std::vector<std::string> remote_names;
	for (const auto &srv : a2a::a2a_server_manager::get_instance().get_all_servers()) {
		remote_names.push_back(srv.name + " (" + srv.url + ")");
	}
	remotes_list->set_items(remote_names);
	auto rem_ptr = remotes_list.get();
	tab3_flow->add_child(std::move(remotes_list));

	auto rem_btns = std::make_unique<ui_buttons_horizontal>("rem_btns");
	rem_btns->add_child(std::make_unique<ui_button>("btn_add_rem", "Add", 'a', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("add_a2a_server");
	}));
	rem_btns->add_child(std::make_unique<ui_button>("btn_edit_rem", "Edit", 'e', [d = dlg.get(), rem_ptr]() {
		int idx = rem_ptr->get_selected_index();
		auto servers = a2a::a2a_server_manager::get_instance().get_all_servers();
		if (idx >= 0 && idx < (int)servers.size()) {
			d->set_action(dialog_result::confirmed);
			d->set_result("edit_a2a_server:" + servers[idx].name);
		}
	}));
	rem_btns->add_child(std::make_unique<ui_button>("btn_del_rem", "Delete", 'd', [d = dlg.get(), rem_ptr]() {
		int idx = rem_ptr->get_selected_index();
		auto servers = a2a::a2a_server_manager::get_instance().get_all_servers();
		if (idx >= 0 && idx < (int)servers.size()) {
			a2a::a2a_server_manager::get_instance().remove_server(servers[idx].name);
			d->set_action(dialog_result::confirmed);
			d->set_result("reopen_a2a_settings");
		}
	}));
	tab3_flow->add_child(std::move(rem_btns));
	tabbed->add_tab_page("remote", "Remote Servers", std::move(tab3_flow));

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

void apply_a2a_settings_from_dialog(const dialog &dlg)
{
	apply_settings_from_dialog(dlg);
}

std::unique_ptr<dialog> create_a2a_servers_dialog(int initial_selection)
{
	auto dlg = std::make_unique<dialog>("A2A Remote Servers", 68, 20);
	auto &manager = a2a::a2a_server_manager::get_instance();
	auto servers = manager.get_all_servers();

	std::vector<std::string> item_labels;
	for (const auto &s : servers) {
		std::string tier_str;
		switch (s.tier) {
		case a2a::a2a_server_tier::ephemeral_runtime: tier_str = "Runtime"; break;
		case a2a::a2a_server_tier::project_local: tier_str = "Project"; break;
		case a2a::a2a_server_tier::global_system: tier_str = "Global"; break;
		}
		item_labels.push_back(std::format("{} -> {} ({})", s.name, s.url, tier_str));
	}

	auto flow = std::make_unique<ui_vertical_flow>("a2a_config_flow", 2, 1, 1);
	int lb_width = std::max(64, std::min(100, COLS - 8));
	auto lb = std::make_unique<ui_listbox>("a2a_server_list", lb_width, 12, nullptr, nullptr);
	lb->set_items(item_labels);
	if (initial_selection >= 0 && initial_selection < (int)servers.size()) {
		lb->set_selected_index(initial_selection);
	}
	auto lb_ptr = lb.get();

	flow->add_child(std::move(lb));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_add", "Add...", 'a', [d = dlg.get()]() {
		d->set_action(dialog_result::confirmed);
		d->set_result("add");
	}));
	btns->add_child(std::make_unique<ui_button>("btn_edit", "Edit...", 'e', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			d->set_action(dialog_result::confirmed);
			d->set_result(std::format("edit:{}", idx));
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_test", "Test/Card", 't', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			d->set_action(dialog_result::confirmed);
			d->set_result(std::format("test:{}", idx));
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_remove", "Remove", 'r', [d = dlg.get(), lb_ptr]() {
		int idx = lb_ptr->get_selected_index();
		if (idx >= 0) {
			d->set_action(dialog_result::confirmed);
			d->set_result(std::format("remove:{}", idx));
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_close", "Close", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
	}));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("a2a_server_list");
	return dlg;
}

std::unique_ptr<dialog> create_a2a_server_edit_dialog(const std::string &initial_name, const std::string &initial_url, const std::string &initial_auth)
{
	auto dlg = std::make_unique<dialog>(initial_name.empty() ? "Add A2A Server" : "Edit A2A Server", 64, 16);
	auto flow = std::make_unique<ui_vertical_flow>("add_a2a_flow", 2, 1, 1);

	flow->add_child(std::make_unique<ui_textbox>("name", 56, initial_name, nullptr, "Handle / Name: "));
	flow->add_child(std::make_unique<ui_textbox>("url", 56, initial_url.empty() ? "http://" : initial_url, nullptr, "Base URL:      "));
	flow->add_child(std::make_unique<ui_textbox>("auth", 56, initial_auth, nullptr, "Auth Token:    "));

	auto btns = std::make_unique<ui_buttons_horizontal>("buttons");
	btns->set_centered(true);
	btns->add_child(std::make_unique<ui_button>("btn_save", "Save", 's', [d = dlg.get()]() {
		auto s_name = d->get_value("name").value_or("");
		auto s_url = d->get_value("url").value_or("");
		if (!s_name.empty() && !s_url.empty()) {
			d->set_action(dialog_result::confirmed);
			d->set_result("save");
		}
	}));
	btns->add_child(std::make_unique<ui_button>("btn_cancel", "Cancel", 'c', [d = dlg.get()]() {
		d->set_action(dialog_result::cancelled);
	}));

	flow->add_child(std::move(btns));

	auto flow_ptr = flow.get();
	dlg->add_child(std::move(flow));

	dlg->flow();
	dlg->set_width(flow_ptr->width());
	dlg->set_height(flow_ptr->height());

	dlg->set_focus_by_name("name");
	return dlg;
}
