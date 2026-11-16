#include "markdown_utils.h"
#include "toolcall_watcher/toolcall_parser.h"
#include "utf8.h"

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <filesystem>
#include <format>
#include <ncurses.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kPairTitleBar = 1;
constexpr int kPairTitleAccent = 2;
constexpr int kPairBody = 3;
constexpr int kPairBold = 4;
constexpr int kPairCode = 5;
constexpr int kPairCodeBlock = 6;
constexpr int kPairAlert = 7;
constexpr int kPollTimeoutMs = 500;

struct styled_span_char {
	std::string utf8;
	int pair = kPairBody;
	int attr = A_NORMAL;
	int width = 1;
};

bool is_box_drawing_char(std::string_view ch) noexcept
{
	return ch == "┌" || ch == "─" || ch == "┬" || ch == "┐" || ch == "│" || ch == "├" || ch == "┼" || ch == "┤" || ch == "└" ||
	       ch == "┴" || ch == "┘";
}

bool is_horizontal_rule(std::string_view line) noexcept
{
	size_t dashes = 0;
	for (char c : line) {
		if (c == '-') {
			dashes++;
		} else if (c != ' ' && c != '\t') {
			return false;
		}
	}
	return dashes >= 3;
}

void draw_truncated_utf8(int row, int start_col, int max_cols, std::string_view text, int pair, int attr)
{
	if (max_cols <= 0) {
		return;
	}
	int col = start_col;
	int used = 0;
	size_t offset = 0;
	std::string ch;
	std::string s(text);

	attron(COLOR_PAIR(pair) | attr);
	while (utf8::next_character(s, offset, ch)) {
		if (ch == "\t") {
			ch = "    ";
		}
		int w = static_cast<int>(utf8::display_width(ch));
		if (w <= 0) {
			w = 1;
		}
		if (used + w > max_cols) {
			break;
		}
		mvaddstr(row, col, ch.c_str());
		col += w;
		used += w;
	}
	attroff(COLOR_PAIR(pair) | attr);
}

std::string fit_utf8_with_ellipsis(std::string_view text, int max_cols)
{
	if (max_cols <= 0) {
		return "";
	}
	std::string s(text);
	if (static_cast<int>(utf8::display_width(s)) <= max_cols) {
		return s;
	}
	if (max_cols <= 3) {
		return std::string(static_cast<size_t>(max_cols), '.');
	}

	int limit = max_cols - 3;
	int used = 0;
	size_t offset = 0;
	std::string out;
	std::string ch;
	while (utf8::next_character(s, offset, ch)) {
		int w = static_cast<int>(utf8::display_width(ch));
		if (used + w > limit) {
			break;
		}
		out += ch;
		used += w;
	}
	out += "...";
	return out;
}

std::vector<styled_span_char> parse_inline_markdown(std::string_view line, int base_pair, int base_attr)
{
	std::vector<styled_span_char> glyphs;
	bool is_bold = false;
	bool is_code = false;
	size_t idx = 0;
	std::string line_str(line);

	while (idx < line_str.size()) {
		if (line_str[idx] == '|' && (idx == 0 || line_str[idx - 1] != '\\')) {
			is_bold = false;
			is_code = false;
		}
		if (!is_code && idx + 1 < line_str.size() && line_str[idx] == '*' && line_str[idx + 1] == '*') {
			is_bold = !is_bold;
			idx += 2;
			continue;
		}
		if (line_str[idx] == '`') {
			is_code = !is_code;
			idx += 1;
			continue;
		}

		unsigned char lead = static_cast<unsigned char>(line_str[idx]);
		size_t char_bytes = utf8::char_len(lead);
		if (idx + char_bytes > line_str.size()) {
			char_bytes = line_str.size() - idx;
		}
		std::string ch = line_str.substr(idx, char_bytes);
		idx += char_bytes;

		if (ch == "\t") {
			ch = "    ";
		}

		styled_span_char sc;
		sc.utf8 = std::move(ch);
		sc.width = std::max(1, static_cast<int>(utf8::display_width(sc.utf8)));
		if (is_box_drawing_char(sc.utf8)) {
			sc.pair = kPairCode;
			sc.attr = A_NORMAL;
		} else if (is_code) {
			sc.pair = kPairCode;
			sc.attr = A_BOLD;
		} else if (is_bold) {
			sc.pair = kPairBold;
			sc.attr = A_BOLD;
		} else {
			sc.pair = base_pair;
			sc.attr = base_attr;
		}
		glyphs.push_back(std::move(sc));
	}
	return glyphs;
}

void draw_title_bar(int cols, std::string_view left_badge, std::string_view call_sig, std::string_view right_label, bool paused)
{
	attron(COLOR_PAIR(kPairTitleBar));
	mvhline(0, 0, ' ', cols);
	attroff(COLOR_PAIR(kPairTitleBar));

	if (cols <= 0) {
		return;
	}

	int right_w = static_cast<int>(utf8::display_width(std::string(right_label)));
	int badge_w = static_cast<int>(utf8::display_width(std::string(left_badge)));

	// Draw left badge ([toolcall.N])
	int badge_pair = paused ? kPairTitleAccent : kPairTitleBar;
	draw_truncated_utf8(0, 1, std::max(0, cols - 1), left_badge, badge_pair, A_BOLD);

	int call_start = 1 + badge_w + 1;
	int right_start = std::max(call_start + 1, cols - right_w - 1);
	int call_max = right_start - call_start - 1;
	if (call_max > 0 && !call_sig.empty()) {
		std::string fitted = fit_utf8_with_ellipsis(call_sig, call_max);
		draw_truncated_utf8(0, call_start, call_max, fitted, kPairTitleBar, A_NORMAL);
	}

	if (right_w > 0 && right_start < cols) {
		draw_truncated_utf8(0, right_start, cols - right_start, right_label, paused ? kPairTitleAccent : kPairTitleBar,
				    paused ? A_BOLD : A_NORMAL);
	}
}

void render_screen(int rows, int cols, const std::filesystem::path &watch_dir, const std::optional<uint64_t> &entry_index,
		   const std::optional<toolcall_watcher::parsed_toolcall> &tc, const std::string &aligned_body, bool auto_follow)
{
	erase();

	if (!tc.has_value() || !entry_index.has_value()) {
		std::string dir_str = watch_dir.string();
		draw_title_bar(cols, "[turbostar-toolcall-watcher]", dir_str, auto_follow ? "LIVE" : "PAUSED", !auto_follow);

		std::string msg = std::format("Waiting for toolcall.* in {}...", dir_str);
		std::string hint = "Keys: Left/Right = step toolcalls | q / Esc = quit";
		int mid_row = std::max(1, rows / 2);
		int msg_w = static_cast<int>(utf8::display_width(msg));
		int hint_w = static_cast<int>(utf8::display_width(hint));

		if (mid_row < rows) {
			draw_truncated_utf8(mid_row, std::max(0, (cols - msg_w) / 2), cols, msg, kPairBold, A_BOLD);
		}
		if (mid_row + 2 < rows) {
			draw_truncated_utf8(mid_row + 2, std::max(0, (cols - hint_w) / 2), cols, hint, kPairCode, A_NORMAL);
		}
		refresh();
		return;
	}

	std::string badge = std::format("[toolcall.{}]", *entry_index);
	std::string right_label = auto_follow ? std::format("[{}]", tc->duration_label)
					      : std::format("[PAUSED] [{}]", tc->duration_label);
	draw_title_bar(cols, badge, tc->c_style_call, right_label, !auto_follow);

	std::istringstream iss(aligned_body);
	std::string line;
	int row = 1;
	bool in_code_block = false;

	while (row < rows && std::getline(iss, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		std::string_view sv(line);
		if (sv.starts_with("```")) {
			in_code_block = !in_code_block;
			draw_truncated_utf8(row, 0, cols, sv, kPairCodeBlock, A_BOLD);
			row++;
			continue;
		}

		if (in_code_block) {
			draw_truncated_utf8(row, 0, cols, sv, kPairCodeBlock, A_NORMAL);
			row++;
			continue;
		}

		if (is_horizontal_rule(sv)) {
			attron(COLOR_PAIR(kPairCode));
			for (int c = 0; c < cols; ++c) {
				mvaddstr(row, c, "─");
			}
			attroff(COLOR_PAIR(kPairCode));
			row++;
			continue;
		}

		int base_pair = kPairBody;
		int base_attr = A_NORMAL;
		if (sv.starts_with('#')) {
			size_t hash_end = 0;
			while (hash_end < sv.size() && sv[hash_end] == '#') {
				hash_end++;
			}
			if (hash_end < sv.size() && sv[hash_end] == ' ') {
				sv.remove_prefix(hash_end + 1);
			}
			base_pair = kPairBold;
			base_attr = A_BOLD;
		} else if (sv.starts_with('>')) {
			base_pair = kPairAlert;
			base_attr = A_NORMAL;
		}

		auto glyphs = parse_inline_markdown(sv, base_pair, base_attr);
		int col = 0;
		for (const auto &g : glyphs) {
			if (col + g.width > cols) {
				break;
			}
			attron(COLOR_PAIR(g.pair) | g.attr);
			mvaddstr(row, col, g.utf8.c_str());
			attroff(COLOR_PAIR(g.pair) | g.attr);
			col += g.width;
		}
		row++;
	}

	refresh();
}

} // namespace

int main(int argc, char *argv[])
{
	std::filesystem::path watch_dir = std::filesystem::current_path();
	if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
		std::filesystem::path untrusted_dir(argv[1]);
		std::error_code ec;
		auto canon = std::filesystem::weakly_canonical(untrusted_dir, ec);
		watch_dir = ec ? untrusted_dir : canon;
	}

	std::setlocale(LC_ALL, "");
	setenv("ESCDELAY", "25", 1);
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	timeout(kPollTimeoutMs);

	if (has_colors()) {
		start_color();
		auto bright = [](short c) -> short {
			return COLORS >= 16 ? static_cast<short>(c + 8) : c;
		};
		init_pair(kPairTitleBar, COLOR_BLACK, COLOR_WHITE);
		init_pair(kPairTitleAccent, COLOR_RED, COLOR_WHITE);
		init_pair(kPairBody, bright(COLOR_WHITE), COLOR_BLUE);
		init_pair(kPairBold, bright(COLOR_YELLOW), COLOR_BLUE);
		init_pair(kPairCode, bright(COLOR_CYAN), COLOR_BLUE);
		init_pair(kPairCodeBlock, bright(COLOR_GREEN), COLOR_BLUE);
		init_pair(kPairAlert, bright(COLOR_CYAN), COLOR_BLUE);
		bkgd(COLOR_PAIR(kPairBody));
	}

	bool auto_follow = true;
	std::optional<uint64_t> active_index;
	std::optional<toolcall_watcher::parsed_toolcall> active_tc;
	std::string aligned_body;
	std::filesystem::file_time_type last_mtime{};
	uintmax_t last_size = 0;
	int last_rows = -1;
	int last_cols = -1;
	bool dirty = true;

	while (true) {
		auto entries = toolcall_watcher::toolcall_parser::scan_directory(watch_dir);
		std::optional<uint64_t> target_index;
		if (!entries.empty()) {
			if (auto_follow || !active_index.has_value()) {
				target_index = entries.back().index;
			} else {
				target_index = active_index;
			}
		} else {
			active_index.reset();
			active_tc.reset();
			aligned_body.clear();
		}

		int rows = 0;
		int cols = 0;
		getmaxyx(stdscr, rows, cols);
		if (rows != last_rows || cols != last_cols) {
			last_rows = rows;
			last_cols = cols;
			dirty = true;
		}

		if (target_index.has_value()) {
			auto it = std::find_if(entries.begin(), entries.end(), [&](const toolcall_watcher::toolcall_entry &e) {
				return e.index == *target_index;
			});
			if (it == entries.end() && !entries.empty()) {
				it = std::prev(entries.end());
				target_index = it->index;
			}
			if (it != entries.end()) {
				std::error_code ec;
				auto mtime = std::filesystem::last_write_time(it->path, ec);
				auto fsize = std::filesystem::file_size(it->path, ec);
				bool file_changed = (!active_index.has_value() || *active_index != it->index || mtime != last_mtime ||
						     fsize != last_size);
				if (file_changed || dirty) {
					if (file_changed) {
						if (auto parsed = toolcall_watcher::toolcall_parser::parse_file(it->path)) {
							active_index = it->index;
							active_tc = std::move(parsed);
							last_mtime = mtime;
							last_size = fsize;
							aligned_body = markdown_utils::align_all_tables(active_tc->body_markdown, true, 0,
													std::max(20, cols));
							dirty = true;
						}
					} else if (active_tc.has_value()) {
						aligned_body = markdown_utils::align_all_tables(active_tc->body_markdown, true, 0,
												std::max(20, cols));
					}
				}
			}
		}

		if (dirty) {
			render_screen(rows, cols, watch_dir, active_index, active_tc, aligned_body, auto_follow);
			dirty = false;
		}

		int ch = getch();
		if (ch == 'q' || ch == 'Q' || ch == 27) {
			break;
		}
		if (ch == KEY_RESIZE) {
			dirty = true;
			continue;
		}
		if (ch == KEY_LEFT && !entries.empty()) {
			auto it = std::find_if(entries.begin(), entries.end(), [&](const toolcall_watcher::toolcall_entry &e) {
				return active_index.has_value() && e.index == *active_index;
			});
			if (it == entries.end()) {
				active_index = entries.back().index;
			} else if (it != entries.begin()) {
				active_index = std::prev(it)->index;
			}
			auto_follow = false;
			last_size = 0; // Force reload on next loop
			dirty = true;
		} else if (ch == KEY_RIGHT && !entries.empty()) {
			if (!auto_follow) {
				auto it = std::find_if(entries.begin(), entries.end(), [&](const toolcall_watcher::toolcall_entry &e) {
					return active_index.has_value() && e.index == *active_index;
				});
				if (it == entries.end() || std::next(it) == entries.end()) {
					auto_follow = true;
					active_index = entries.back().index;
				} else {
					active_index = std::next(it)->index;
					if (std::next(it) == std::prev(entries.end())) {
						auto_follow = true;
					}
				}
				last_size = 0;
				dirty = true;
			}
		}
	}

	endwin();
	return 0;
}
