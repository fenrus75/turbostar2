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

struct legacy_ui_button {
	std::string text;
	char hotkey;
	std::string result_string;
	dialog_result action;
	int x;
	int y;
	int width;
	bool is_focused{false};

	legacy_ui_button(const std::string &t, char hk, const std::string &res, dialog_result act, int px, int py);

	bool contains(int mouse_x, int mouse_y) const {
		return mouse_y == y && mouse_x >= x && mouse_x < x + width;
	}

	void set_focus(bool focused) {
		is_focused = focused;
	}

	void draw() const;
};

class dialog : public ui_container
{
      public:
	dialog(const std::string &title, int width, int height);
	virtual ~dialog() = default;

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

	// Legacy support methods
	virtual void draw() const;
	virtual dialog_result handle_key(int key);
	virtual std::optional<dialog_result> handle_mouse(int x, int y);
	virtual std::string get_result() const { return result_string_; }

	void set_result(const std::string& res) { result_string_ = res; }
	void set_action(dialog_result action) { action_ = action; }
	dialog_result get_action() const { return action_; }

      protected:
	std::string title_;
	mutable std::vector<legacy_ui_button> buttons_;
	std::string result_string_;
	dialog_result action_{dialog_result::pending};

	void draw_buttons(int focused_button_idx) const;
};

#include "document.h"

// Factory functions
std::unique_ptr<dialog> create_save_prompt_dialog(const std::string& filename);
std::unique_ptr<dialog> create_input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value = "");
std::unique_ptr<dialog> create_search_dialog(const std::string &title, const search_params &initial_params, bool is_replace);

search_params extract_search_params(const dialog& dlg, const search_params& initial_params);

// Legacy classes...

class message_dialog : public dialog {
      public:
	message_dialog(const std::string &title, const std::vector<std::string> &lines);
	void draw() const override;
	dialog_result handle_key(int key) override;
      private:
	std::vector<std::string> lines_;
};

class force_quit_dialog : public dialog {
      public:
	force_quit_dialog();
	void draw() const override;
	dialog_result handle_key(int key) override;
	std::optional<dialog_result> handle_mouse(int x, int y) override;
	std::string get_result() const override;
	bool tick();
      private:
	int focus_idx_{1};
	bool countdown_active_{true};
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	int remaining_seconds_{5};
};

class ask_user_dialog : public dialog {
      public:
	ask_user_dialog(const std::string& question, const std::vector<std::string>& options);
	void draw() const override;
	dialog_result handle_key(int key) override;
	std::optional<dialog_result> handle_mouse(int x, int y) override;
	std::string get_result() const override;
      private:
	std::string question_;
	std::vector<std::string> options_;
	int focus_idx_{0};
	std::string custom_answer_;
};
