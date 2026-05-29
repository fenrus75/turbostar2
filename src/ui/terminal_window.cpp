#include "terminal_window.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ncurses.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../command_runner.h"
#include "build_error_manager.h"
#include "gcc_log_parser.h"

#include <map>
#include <utility>

namespace ui
{

static int get_terminal_color_pair(uint8_t fg, uint8_t bg)
{
	static std::map<std::pair<uint8_t, uint8_t>, int> allocated_pairs;
	static int next_pair = 100;

	uint8_t f = fg & 0xF;
	uint8_t b = bg & 0xF;
	auto key = std::make_pair(f, b);
	auto it = allocated_pairs.find(key);
	if (it != allocated_pairs.end()) {
		return it->second;
	}

	int pair = next_pair++;
	if (pair < COLOR_PAIRS) {
		init_pair(pair, f, b);
	} else {
		pair = 0;
	}
	allocated_pairs[key] = pair;
	return pair;
}

terminal_window::terminal_window(int id, int x, int y, int width, int height, const std::string &title)
    : ::window(id, x, y, width, height, title), emulator_(width - 2, height - 2)
{
	background_color_pair_ = 29; // default terminal color: white on black
}

terminal_window::~terminal_window()
{
	stop_process();
	if (pty_master_ >= 0) {
		close(pty_master_);
	}
}

bool terminal_window::start_process(const std::string &raw_command, std::unique_ptr<build_log_parser> parser)
{
	stop_process();
	parser_ = std::move(parser);
	line_count_ = 0;
	line_buffer_.clear();

	if (pty_master_ >= 0) {
		close(pty_master_);
		pty_master_ = -1;
	}

	pty_master_ = posix_openpt(O_RDWR | O_NOCTTY);
	if (pty_master_ < 0) {
		return false;
	}
	if (grantpt(pty_master_) < 0 || unlockpt(pty_master_) < 0) {
		close(pty_master_);
		pty_master_ = -1;
		return false;
	}

	char *slave_name = ptsname(pty_master_);
	if (!slave_name) {
		close(pty_master_);
		pty_master_ = -1;
		return false;
	}

	// Build the sandboxed command using command_runner
	sync_command_runner runner;
	runner.apply_build_profile();
	runner.set_use_pty(true);
	runner.set_enable_crash_catcher(true);
	std::string sandboxed_cmd = runner.build_command(raw_command);

	pid_ = fork();
	if (pid_ < 0) {
		close(pty_master_);
		pty_master_ = -1;
		return false;
	}

	if (pid_ == 0) {
		// Child process
		int slave_fd = open(slave_name, O_RDWR);
		close(pty_master_);

		// Set PTY terminal size to match the window content area
		struct winsize ws;
		ws.ws_row = height_ - 2;
		ws.ws_col = width_ - 2;
		ws.ws_xpixel = 0;
		ws.ws_ypixel = 0;
		ioctl(slave_fd, TIOCSWINSZ, &ws);

		dup2(slave_fd, STDIN_FILENO);
		dup2(slave_fd, STDOUT_FILENO);
		dup2(slave_fd, STDERR_FILENO);
		if (slave_fd > STDERR_FILENO) {
			close(slave_fd);
		}

		setsid();
		ioctl(0, TIOCSCTTY, 1);

		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGTTIN, SIG_DFL);
		signal(SIGTTOU, SIG_DFL);

		execl("/bin/bash", "bash", "-c", sandboxed_cmd.c_str(), nullptr);
		_exit(127);
	}

	is_alive_ = true;
	// Set PTY master to non-blocking
	int flags = fcntl(pty_master_, F_GETFL, 0);
	fcntl(pty_master_, F_SETFL, flags | O_NONBLOCK);

	return true;
}

void terminal_window::stop_process()
{
	if (pid_ > 0 && is_alive_) {
		kill(-pid_, SIGTERM);
		usleep(50000); // Wait 50ms
		int status;
		if (waitpid(pid_, &status, WNOHANG) == 0) {
			kill(-pid_, SIGKILL);
			waitpid(pid_, &status, 0);
		}
		is_alive_ = false;
		pid_ = -1;
	}
}

bool terminal_window::update_pty()
{
	if (pty_master_ < 0)
		return false;

	char buf[1024];
	bool new_data = false;
	while (is_alive_) {
		ssize_t bytes = read(pty_master_, buf, sizeof(buf));
		if (bytes > 0) {
			std::string chunk(buf, bytes);
			emulator_.write(chunk);

			if (parser_) {
				line_buffer_ += chunk;
				size_t pos;
				while ((pos = line_buffer_.find('\n')) != std::string::npos) {
					std::string line = line_buffer_.substr(0, pos);
					if (!line.empty() && line.back() == '\r') {
						line.pop_back();
					}

					// Strip ANSI escape codes so GCC log parser isn't confused by colors
					std::string stripped_line;
					for (size_t i = 0; i < line.length(); ++i) {
						if (line[i] == '\x1b') {
							if (i + 1 < line.length() && line[i + 1] == '[') {
								size_t j = i + 2;
								while (j < line.length() && !((line[j] >= 'a' && line[j] <= 'z') ||
											      (line[j] >= 'A' && line[j] <= 'Z'))) {
									j++;
								}
								if (j < line.length()) {
									i = j;
									continue;
								}
							}
						}
						stripped_line += line[i];
					}

					std::vector<build_error> errs;
					parser_->parse_line(stripped_line, line_count_++, errs);
					for (const auto &e : errs) {
						build_error_manager::get_instance().add_error(e);
					}
					line_buffer_ = line_buffer_.substr(pos + 1);
				}
			}

			new_data = true;
		} else if (bytes < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				break; // no more data right now
			} else {
				is_alive_ = false;
				break;
			}
		} else {
			// EOF (child exited)
			is_alive_ = false;
			break;
		}
	}

	if (!is_alive_ && pid_ > 0) {
		int status;
		if (waitpid(pid_, &status, WNOHANG) > 0) {
			pid_ = -1;
		}
	}

	if (new_data) {
		invalidate();
	}
	return new_data;
}

bool terminal_window::process_events()
{
	bool needs_redraw = false;
	auto &queue = get_window_queue();

	while (auto ev = queue.pop()) {
		if (ev->type != event_type::key_press)
			continue;

		if (ev->key_code == 7) { // Ctrl-G
			capture_input_ = !capture_input_;
			needs_redraw = true;
			continue;
		}

		if (capture_input_) {
			if (pty_master_ >= 0 && is_alive_) {
				std::string seq;
				if (!ev->utf8_char.empty()) {
					seq = ev->utf8_char;
				} else {
					switch (ev->key_code) {
						case KEY_UP:
							seq = "\x1b[A";
							break;
						case KEY_DOWN:
							seq = "\x1b[B";
							break;
						case KEY_RIGHT:
							seq = "\x1b[C";
							break;
						case KEY_LEFT:
							seq = "\x1b[D";
							break;
						case KEY_HOME:
							seq = "\x1b[H";
							break;
						case KEY_END:
							seq = "\x1b[F";
							break;
						case KEY_PPAGE:
							seq = "\x1b[5~";
							break;
						case KEY_NPAGE:
							seq = "\x1b[6~";
							break;
						case KEY_DC:
							seq = "\x1b[3~";
							break;
						case KEY_BACKSPACE:
						case 127:
						case 8:
							seq = "\x7f";
							break;
						case 13:
						case 10:
						case KEY_ENTER:
							seq = "\r";
							break;
						case 27:
							seq = "\x1b";
							break;
						default:
							break;
					}
				}
				if (!seq.empty()) {
					ssize_t w = write(pty_master_, seq.data(), seq.size());
					(void)w;
				}
			}
		} else {
			// Re-capture input on pressing Enter
			if (ev->key_code == 13 || ev->key_code == 10 || ev->key_code == KEY_ENTER) {
				capture_input_ = true;
				needs_redraw = true;
			} else {
				// Pass event to base class (e.g. key_press select_window, etc.)
				// (Since we don't have doc_, it won't do doc-editing, which is correct).
			}
		}
	}
	return needs_redraw;
}

void terminal_window::set_cursor_position() const
{
	if (capture_input_) {
		int screen_y = y_ + 1 + emulator_.get_cursor_y();
		int screen_x = x_ + 1 + emulator_.get_cursor_x();
		move(screen_y, screen_x);
	}
}

bool terminal_window::is_cursor_visible() const
{
	return emulator_.is_cursor_visible();
}

void terminal_window::draw_content() const
{
	const auto &grid = emulator_.get_grid();
	for (int y = 0; y < height_ - 2; ++y) {
		if (y >= static_cast<int>(grid.size()))
			break;
		move(y_ + 1 + y, x_ + 1);
		const auto &row = grid[y];
		for (int x = 0; x < width_ - 2; ++x) {
			if (x >= static_cast<int>(row.size()))
				break;
			const auto &cell = row[x];

			int pair = get_terminal_color_pair(cell.fg, cell.bg);
			int attrs = COLOR_PAIR(pair);
			if (cell.bold)
				attrs |= A_BOLD;
			if (cell.reverse)
				attrs |= A_REVERSE;

			attrset(attrs);
			addstr(cell.glyph.c_str());
		}
	}
}

void terminal_window::draw_border() const
{
	::window::draw(); // Draw base frame and title

	attrset(COLOR_PAIR(5));
	if (capture_input_) {
		std::string status = "[Capture: ON (^G to escape)]";
		mvprintw(y_ + height_ - 1, x_ + width_ - status.length() - 2, " %s ", status.c_str());
	} else {
		std::string status = "[Capture: OFF (Click/Enter to capture)]";
		mvprintw(y_ + height_ - 1, x_ + width_ - status.length() - 2, " %s ", status.c_str());
	}
	attrset(0);
}

} // namespace ui
