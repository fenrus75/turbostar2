#pragma once

#include <string>
#include <vector>

/**
 * @brief Possible results of a modal dialog interaction.
 */
enum class dialog_result {
	pending,   ///< Interaction still in progress
	confirmed, ///< User accepted (e.g., Enter)
	cancelled  ///< User aborted (e.g., Esc)
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
