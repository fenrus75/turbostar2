#pragma once

#include <string>
#include <vector>
#include <chrono>

/**
 * @brief Possible results of a modal dialog interaction.
 */
enum class dialog_result {
	pending,   ///< Interaction still in progress
	confirmed, ///< User accepted (e.g., Enter)
	cancelled  ///< User aborted (e.g., Esc)
};

/**
 * @brief Represents a clickable button inside a dialog.
 */
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

/**
 * @brief Abstract base class for modal dialog boxes.
 *
 * Handles layout, borders, and shadows.
 */
class dialog
{
      public:
	dialog(const std::string &title, int width, int height);
	virtual ~dialog() = default;

	/**
	 * @brief Renders the dialog box to the screen.
	 */
	virtual void draw() const;

	/**
	 * @brief Processes keyboard input for the dialog.
	 * @param key The key code to process.
	 * @return The result of the interaction.
	 */
	virtual dialog_result handle_key(int key) = 0;

	/**
	 * @brief Processes mouse input for the dialog.
	 * @return A dialog_result if an action was taken, or nullopt.
	 */
	virtual std::optional<dialog_result> handle_mouse(int /*x*/, int /*y*/) { return std::nullopt; }

	/**
	 * @brief Returns the primary result string of the dialog (if
	 * applicable).
	 */
	virtual std::string get_result() const
	{
		return "";
	}

      protected:
	std::string title_;
	int width_, height_;
	int x_, y_;
	mutable std::vector<legacy_ui_button> buttons_;

	void draw_buttons(int focused_button_idx) const;
};

/**
 * @brief A dialog box for capturing a single line of text input.
 */
class input_dialog : public dialog
{
      public:
	input_dialog(const std::string &title, const std::string &prompt, const std::string &initial_value = "");
	~input_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	std::string get_result() const override;

      private:
	std::string prompt_;
	std::string buffer_;
};

/**
 * @brief A dialog box for displaying multiple lines of text with an OK button.
 */
class message_dialog : public dialog
{
      public:
	message_dialog(const std::string &title, const std::vector<std::string> &lines);
	~message_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;

      private:
	std::vector<std::string> lines_;
};

/**
 * @brief A dialog box asking to save a modified document before closing.
 */
class save_prompt_dialog : public dialog
{
      public:
	save_prompt_dialog(const std::string &filename);
	~save_prompt_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	std::optional<dialog_result> handle_mouse(int x, int y) override;
	std::string get_result() const override;

      private:
	std::string filename_;
	int focus_idx_{0}; // 0 = Save, 1 = Discard, 2 = Cancel
	
};

/**
 * @brief A dialog box asking to save all or exit when force quitting.
 */
class force_quit_dialog : public dialog
{
      public:
	force_quit_dialog();
	~force_quit_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	std::optional<dialog_result> handle_mouse(int x, int y) override;
	std::string get_result() const override;
	bool tick();

      private:
	int focus_idx_{1}; // 0 = Exit, 1 = Save All, 2 = Cancel

	bool countdown_active_{true};
	std::chrono::time_point<std::chrono::steady_clock> start_time_;
	int remaining_seconds_{5};
};

/**
 * @brief A dialog box asking a question with multiple choices.
 */
class ask_user_dialog : public dialog
{
      public:
	ask_user_dialog(const std::string& question, const std::vector<std::string>& options);
	~ask_user_dialog() override = default;

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
