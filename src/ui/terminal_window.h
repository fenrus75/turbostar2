#pragma once

#include <string>
#include <sys/types.h>
#include "ansi_terminal_emulator.h"
#include "window.h"

class build_log_parser;

namespace ui
{

class terminal_window : public ::window
{
      public:
	terminal_window(int id, int x, int y, int width, int height, const std::string &title);
	virtual ~terminal_window();

	// Spawn the process in a PTY. Uses command_runner internally for sandboxing.
	bool start_process(const std::string &raw_command, std::unique_ptr<build_log_parser> parser = nullptr, bool enable_network = false, bool enable_crash_catcher = true);

	// Stop process (sends SIGTERM/SIGKILL)
	void stop_process();

	// Overrides
	bool process_events() override;
	void set_cursor_position() const override;
	bool is_cursor_visible() const override;

	// Checks master PTY for output and updates emulator. Returns true if new output was processed.
	bool update_pty();

	int get_pty_master_fd() const
	{
		return pty_master_;
	}
	bool is_alive() const
	{
		return pid_ > 0 && is_alive_;
	}

	void set_capture_input(bool capture)
	{
		capture_input_ = capture;
	}
	bool get_capture_input() const
	{
		return capture_input_;
	}

	struct screenshot_data {
		std::vector<std::string> grid;
		int cursor_x = 0;
		int cursor_y = 0;
		bool cursor_visible = false;
	};
	screenshot_data get_screenshot() const;

      protected:
	void draw_content() const override;
	void draw_border() const override;

      private:
	struct debug_button {
		std::string label;
		std::string seq;
		int start_x;
		int end_x;
	};
	std::vector<debug_button> get_debug_buttons() const;

	ansi_terminal_emulator emulator_;
	int pty_master_{-1};
	pid_t pid_{-1};
	bool is_alive_{false};
	bool capture_input_{true};

	std::unique_ptr<build_log_parser> parser_;
	int line_count_{0};
	std::string line_buffer_;

	int input_fifo_fd_{-1};
	std::string input_fifo_path_;

      public:
	void set_input_fifo(const std::string &path);
};

} // namespace ui
