// Tested source file: src/ui/dialog_ai.cpp, src/ui/dialog_settings.cpp, src/ui/dialog_a2a.cpp, src/editor_events_key.cpp
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include "a2a/a2a_server_manager.h"
#include "agentlib/model_server.h"
#include "editor.h"
#include "test_watchdog.h"
#include "ui/components/ui_listbox.h"
#include "ui/components/ui_radio.h"
#include "ui/components/ui_tabbed_container.h"
#include "ui/components/ui_textbox.h"
#include "ui/dialog.h"
#include "ui/dialog_factories.h"

static ui_element *find_element_by_name(ui_element *elem, const std::string &target_name)
{
	if (!elem)
		return nullptr;
	if (elem->name() == target_name)
		return elem;
	auto *container = dynamic_cast<ui_container *>(elem);
	if (container) {
		for (const auto &child : container->children()) {
			auto *found = find_element_by_name(child.get(), target_name);
			if (found)
				return found;
		}
	}
	auto *tabbed = dynamic_cast<ui_tabbed_container *>(elem);
	if (tabbed) {
		for (size_t i = 0;; ++i) {
			auto *page = tabbed->get_tab_page(i);
			if (!page)
				break;
			auto *found = find_element_by_name(page, target_name);
			if (found)
				return found;
		}
	}
	return nullptr;
}

void test_options_dialogs_flow()
{
	editor_options opts;
	opts.filenames = {};
	opts.no_lsp = true;
	opts.no_welcome = true;

	editor ed(opts);

	// 1. Open AI & Agent Settings dialog
	editor_event open_ev;
	open_ev.type = event_type::ai_settings_config;
	ed.dispatch_event_ui(open_ev);
	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "AI & Agent Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);

	// 2. Click "Add" on the Model Providers tab
	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("add_model_server");
	ed.resolve_dialog(dialog_result::confirmed);

	// Should now be on the "Add Server" dialog
	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "Add Server");

	// 3. Fill in server details and click "OK"
	auto *id_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "id"));
	auto *name_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "name"));
	auto *url_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "url"));
	auto *key_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "api_key"));
	assert(id_tb && name_tb && url_tb && key_tb);

	id_tb->set_buffer("custom_provider_1");
	name_tb->set_buffer("My Custom Provider");
	url_tb->set_buffer("https://api.example.com/v1");
	key_tb->set_buffer("sk-test-12345");

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("ok");
	ed.resolve_dialog(dialog_result::confirmed);

	// 4. Verify server was added to model_server_registry
	auto added_srv = agentlib::model_server_registry::get_instance().get_server("custom_provider_1");
	assert(added_srv != nullptr);
	assert(added_srv->get_name() == "My Custom Provider");
	assert(added_srv->get_url() == "https://api.example.com/v1");
	assert(added_srv->get_api_key() == "sk-test-12345");

	// 5. Verify we returned to "AI & Agent Settings" on the "Model Providers" tab with focus on the newly added item
	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "AI & Agent Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);

	auto *tabbed = dynamic_cast<ui_tabbed_container *>(find_element_by_name(ed.active_dialog_.get(), "ai_tabbed"));
	assert(tabbed != nullptr);
	auto *srv_list = dynamic_cast<ui_listbox *>(find_element_by_name(ed.active_dialog_.get(), "server_list"));
	assert(srv_list != nullptr);
	assert(tabbed->get_active_page() != nullptr);
	assert(find_element_by_name(tabbed->get_active_page(), "server_list") == srv_list);
	assert(srv_list->has_focus());

	auto all_servers = agentlib::model_server_registry::get_instance().get_all_servers();
	int expected_idx = -1;
	for (size_t i = 0; i < all_servers.size(); ++i) {
		if (all_servers[i]->get_id() == "custom_provider_1") {
			expected_idx = static_cast<int>(i);
			break;
		}
	}
	assert(expected_idx >= 0);
	assert(srv_list->get_selected_index() == expected_idx);

	// 6. Click "Edit" on the newly added server in the Model Providers tab
	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("edit_model_server:custom_provider_1");
	ed.resolve_dialog(dialog_result::confirmed);

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "Edit Server");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::model_server_edit);

	auto *edit_name_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "name"));
	auto *edit_url_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "url"));
	assert(edit_name_tb && edit_url_tb);
	assert(ed.active_dialog_->get_value("name") == "My Custom Provider");
	edit_name_tb->set_buffer("Updated Custom Provider");
	edit_url_tb->set_buffer("https://api.updated.example.com/v1");

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("ok");
	ed.resolve_dialog(dialog_result::confirmed);

	auto edited_srv = agentlib::model_server_registry::get_instance().get_server("custom_provider_1");
	assert(edited_srv != nullptr);
	assert(edited_srv->get_name() == "Updated Custom Provider");
	assert(edited_srv->get_url() == "https://api.updated.example.com/v1");

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "AI & Agent Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);

	auto *srv_list_after_edit = dynamic_cast<ui_listbox *>(find_element_by_name(ed.active_dialog_.get(), "server_list"));
	assert(srv_list_after_edit != nullptr);
	assert(srv_list_after_edit->has_focus());
	assert(srv_list_after_edit->get_selected_index() == expected_idx);

	// 7. Register a downloaded model associated with custom_provider_1, then click "Delete"
	auto dl_model = std::make_shared<agentlib::ai_model>(
	    "custom_provider_1_dl_model", "Downloaded Model", "", "", 0.0, 0.0, "",
	    agentlib::api_type::openai, 128000, agentlib::model_cost_type::paid_per_token,
	    "custom_provider_1", true /*from_download*/);
	agentlib::ai_model_registry::get_instance().register_model(dl_model);
	assert(agentlib::ai_model_registry::get_instance().get_model("custom_provider_1_dl_model") != nullptr);

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("delete_model_server:custom_provider_1");
	ed.resolve_dialog(dialog_result::confirmed);

	assert(agentlib::model_server_registry::get_instance().get_server("custom_provider_1") == nullptr);
	assert(agentlib::ai_model_registry::get_instance().get_model("custom_provider_1_dl_model") == nullptr);

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "AI & Agent Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);
	auto *srv_list_after_del = dynamic_cast<ui_listbox *>(find_element_by_name(ed.active_dialog_.get(), "server_list"));
	assert(srv_list_after_del != nullptr);
	assert(srv_list_after_del->has_focus());

	// 8. Test A2A & Remote Settings -> Add A2A Server -> Save -> Edit -> Save -> Delete
	editor_event a2a_open_ev;
	a2a_open_ev.type = event_type::a2a_settings_config;
	ed.dispatch_event_ui(a2a_open_ev);
	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "A2A & Remote Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("add_a2a_server");
	ed.resolve_dialog(dialog_result::confirmed);

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "Add A2A Server");

	auto *a2a_name_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "name"));
	auto *a2a_url_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "url"));
	auto *a2a_auth_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "auth"));
	assert(a2a_name_tb && a2a_url_tb && a2a_auth_tb);
	a2a_name_tb->set_buffer("remote_agent_1");
	a2a_url_tb->set_buffer("http://127.0.0.1:9001");
	a2a_auth_tb->set_buffer("bearer_xyz");

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("save");
	ed.resolve_dialog(dialog_result::confirmed);

	auto added_a2a = a2a::a2a_server_manager::get_instance().find_server("remote_agent_1");
	assert(added_a2a.has_value());
	assert(added_a2a->url == "http://127.0.0.1:9001");
	assert(added_a2a->auth_token == "bearer_xyz");

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "A2A & Remote Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);

	auto *rem_list = dynamic_cast<ui_listbox *>(find_element_by_name(ed.active_dialog_.get(), "remote_list"));
	assert(rem_list != nullptr);
	assert(rem_list->has_focus());

	// Edit A2A server and rename it
	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("edit_a2a_server:remote_agent_1");
	ed.resolve_dialog(dialog_result::confirmed);

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "Edit A2A Server");
	auto *edit_a2a_name_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "name"));
	auto *edit_a2a_url_tb = dynamic_cast<ui_textbox *>(find_element_by_name(ed.active_dialog_.get(), "url"));
	assert(edit_a2a_name_tb && edit_a2a_url_tb);
	edit_a2a_name_tb->set_buffer("remote_agent_renamed");
	edit_a2a_url_tb->set_buffer("http://127.0.0.1:9002");

	ed.active_dialog_->set_action(dialog_result::confirmed);
	ed.active_dialog_->set_result("save");
	ed.resolve_dialog(dialog_result::confirmed);

	assert(!a2a::a2a_server_manager::get_instance().find_server("remote_agent_1").has_value());
	auto renamed_a2a = a2a::a2a_server_manager::get_instance().find_server("remote_agent_renamed");
	assert(renamed_a2a.has_value());
	assert(renamed_a2a->url == "http://127.0.0.1:9002");

	assert(ed.active_dialog_ != nullptr);
	assert(ed.active_dialog_->get_title() == "A2A & Remote Settings");
	assert(ed.active_dialog_mode_ == editor::dialog_mode::settings);
}

int main()
{
	test_watchdog::setup_watchdog(30);
	test_watchdog::scoped_test_home home_guard("test_options_dialogs_home");

	std::cout << "Testing restructured 3 Options tabbed dialogs in isolated HOME..." << std::endl;

	// 1. Test Editor & Workspace Settings Dialog
	auto editor_dlg = create_editor_settings_dialog();
	assert(editor_dlg != nullptr);
	assert(editor_dlg->get_title() == "Editor & Workspace Settings");
	editor_dlg->flow();
	apply_editor_settings_from_dialog(*editor_dlg);

	// 2. Test AI & Agent Settings Dialog (with dedicated Security tab)
	auto ai_dlg = create_ai_settings_dialog();
	assert(ai_dlg != nullptr);
	assert(ai_dlg->get_title() == "AI & Agent Settings");
	ai_dlg->flow();
	assert(ai_dlg->get_value("paranoid_mode").has_value());
	assert(ai_dlg->get_value("run_outside_sandbox").has_value());
	assert(ai_dlg->get_value("shell_display_access").has_value());
	apply_ai_settings_from_dialog(*ai_dlg);

	// 3. Test A2A & Remote Settings Dialog
	auto a2a_dlg = create_a2a_settings_dialog();
	assert(a2a_dlg != nullptr);
	assert(a2a_dlg->get_title() == "A2A & Remote Settings");
	a2a_dlg->flow();
	apply_a2a_settings_from_dialog(*a2a_dlg);

	// 4. Test adding a new model provider from AI & Agent Settings
	test_options_dialogs_flow();

	std::cout << "All 3 Options tabbed dialogs created and verified successfully!" << std::endl;
	return 0;
}
