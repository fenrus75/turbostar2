#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <memory>
#include "ui/ui_element.h"

enum class dialog_result {
        pending,
        confirmed,
        cancelled
};

class dialog : public ui_container{
      public:
	dialog(const std::string &title, int width, int height);
	virtual ~dialog() = default;

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

	// Legacy support methods
	virtual void draw() const;
	virtual dialog_result handle_key(int key);
	virtual bool tick() { return false; }
	virtual std::optional<dialog_result> handle_mouse(int x, int y);
	virtual std::string get_result() const { return result_string_; }

	void set_result(const std::string& res) { result_string_ = res; }
	void set_action(dialog_result action) { action_ = action; }
	dialog_result get_action() const { return action_; }

      protected:
	std::string title_;
	std::string result_string_;
	dialog_result action_{dialog_result::pending};
};

#include "document.h"

// Factory functions
std::unique_ptr<dialog> create_save_prompt_dialog(const std::string& filename);
std::unique_ptr<dialog> create_input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value = "");
std::unique_ptr<dialog> create_search_dialog(const std::string &title, const search_params &initial_params, bool is_replace);

search_params extract_search_params(const dialog& dlg, const search_params& initial_params);
std::unique_ptr<dialog> create_message_dialog(const std::string &title, const std::vector<std::string> &lines);
std::unique_ptr<dialog> create_ask_user_dialog(const std::string& question, const std::vector<std::string>& options);
std::unique_ptr<dialog> create_force_quit_dialog();
std::unique_ptr<dialog> create_settings_dialog();
void apply_settings_from_dialog(const dialog& dlg);
std::unique_ptr<dialog> create_file_dialog(const std::string &title, const std::string &initial_path);

