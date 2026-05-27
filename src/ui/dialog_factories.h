#pragma once
#include "ui/dialog.h"
#include "document.h"
#include "agentlib/ai_model.h"

// Factory functions
std::unique_ptr<dialog> create_save_prompt_dialog(const std::string& filename);
std::unique_ptr<dialog> create_input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value = "");
std::unique_ptr<dialog> create_search_dialog(const std::string &title, const search_params &initial_params, bool is_replace);

search_params extract_search_params(const dialog& dlg, const search_params& initial_params);
std::unique_ptr<dialog> create_message_dialog(const std::string &title, const std::vector<std::string> &lines);
std::unique_ptr<dialog> create_welcome_dialog();
std::unique_ptr<dialog> create_ask_user_dialog(const std::string& question, const std::vector<std::string>& options);
std::unique_ptr<dialog> create_plan_approval_dialog(const std::string& plan_text);
std::unique_ptr<dialog> create_force_quit_dialog();
std::unique_ptr<dialog> create_settings_dialog();
void apply_settings_from_dialog(const dialog& dlg);
std::unique_ptr<dialog> create_file_dialog(const std::string &title, const std::string &initial_path);

std::unique_ptr<dialog> create_model_list_dialog();
std::unique_ptr<dialog> create_model_selection_dialog();
std::unique_ptr<dialog> create_model_edit_dialog(std::shared_ptr<agentlib::ai_model> model);
void apply_model_edit_from_dialog(const dialog& dlg, const std::string& original_id);
