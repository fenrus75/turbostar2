#include "ui/hex_editor_window.h"
#include <algorithm>
#include <format>
#include <iomanip>
#include <ncurses.h>
#include <sstream>
#include "ui/hex_highlighter.h"

hex_editor_window::hex_editor_window(int id, int x, int y, int width, int height, const std::string &title,
				     std::shared_ptr<binary_document> doc)
    : window(id, x, y, width, height, title)
{
	attach_document(doc);
	// Pair 3 is classic window background (yellow on dark blue)
	set_background_color_pair(3);
}

void hex_editor_window::draw_content(bool /*cursor_only*/) const
{
	if (!is_visible()) {
		return;
	}

	auto bin_doc = std::dynamic_pointer_cast<binary_document>(doc_);
	if (!bin_doc)
		return;

	size_t bytes_per_line = get_bytes_per_line();
	int lines_fit = height_ - 2;

	// Scroll management
	size_t cursor_line = cursor_offset_ / bytes_per_line;
	if (cursor_line < scroll_line_) {
		scroll_line_ = cursor_line;
	} else if (cursor_line >= scroll_line_ + lines_fit) {
		scroll_line_ = cursor_line - lines_fit + 1;
	}

	int start_x = x_ + 1;
	int max_content_w = width_ - 2;

	for (int i = 0; i < lines_fit; ++i) {
		size_t line_idx = scroll_line_ + i;
		size_t line_offset = line_idx * bytes_per_line;

		move(y_ + 1 + i, start_x);

		if (line_offset > bin_doc->size() ||
		    (line_offset == bin_doc->size() && bin_doc->size() > 0 && cursor_offset_ != bin_doc->size())) {
			// Print entirely empty line
			for (int j = 0; j < max_content_w; ++j) {
				addch(' ');
			}
			continue;
		}

		// 1. Draw Offset Column (Pair 5 is Bright White on Dark Blue)
		attron(COLOR_PAIR(5));
		printw("%08X", static_cast<unsigned int>(line_offset));
		attroff(COLOR_PAIR(5));
		printw("  ");

		// 2. Draw Hex Column
		for (size_t j = 0; j < bytes_per_line; ++j) {
			size_t byte_offset = line_offset + j;
			size_t x_pos = start_x + 10 + j * 3 + (j / 8);

			move(y_ + 1 + i, x_pos);

			if (byte_offset < bin_doc->size()) {
				uint8_t val = bin_doc->get_byte(byte_offset);
				int pair = get_color_pair_for_byte(byte_offset, val, bin_doc);

				attron(COLOR_PAIR(pair));
				printw("%02X", val);
				attroff(COLOR_PAIR(pair));
				addch(' ');
			} else {
				printw("   ");
			}
		}

		// 3. Draw ASCII Column
		size_t ascii_start_x = start_x + 10 + bytes_per_line * 3 + (bytes_per_line / 8) + 1;
		mvaddstr(y_ + 1 + i, ascii_start_x, "│");

		for (size_t j = 0; j < bytes_per_line; ++j) {
			size_t byte_offset = line_offset + j;
			size_t x_pos = ascii_start_x + 1 + j;

			move(y_ + 1 + i, x_pos);

			if (byte_offset < bin_doc->size()) {
				uint8_t val = bin_doc->get_byte(byte_offset);
				char c = (val >= 32 && val <= 126) ? static_cast<char>(val) : '.';
				int pair = get_color_pair_for_byte(byte_offset, val, bin_doc);

				attron(COLOR_PAIR(pair));
				addch(c);
				attroff(COLOR_PAIR(pair));
			} else {
				addch(' ');
			}
		}
		mvaddstr(y_ + 1 + i, ascii_start_x + 1 + bytes_per_line, "│");

		// Pad rest of the line with spaces
		int printed_w = 10 + bytes_per_line * 3 + (bytes_per_line / 8) + 2 + bytes_per_line + 1;
		if (printed_w < max_content_w) {
			for (int j = printed_w; j < max_content_w; ++j) {
				addch(' ');
			}
		}
	}
}

bool hex_editor_window::process_events()
{
	bool redrawn = false;
	auto bin_doc = std::dynamic_pointer_cast<binary_document>(doc_);
	if (!bin_doc)
		return false;

	size_t bytes_per_line = get_bytes_per_line();
	int lines_fit = height_ - 2;

	while (auto ev_opt = get_window_queue().pop()) {
		if (ev_opt->type == event_type::key_press) {
			int key = ev_opt->key_code;

			if (key == KEY_LEFT || key == 2) { // Left (Ctrl-B fallback)
				if (hex_focus_) {
					if (nibble_focus_ == 1) {
						nibble_focus_ = 0;
					} else {
						if (cursor_offset_ > 0) {
							cursor_offset_--;
							nibble_focus_ = 1;
						}
					}
				} else {
					if (cursor_offset_ > 0) {
						cursor_offset_--;
					}
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_RIGHT || key == 6) { // Right (Ctrl-F fallback)
				if (hex_focus_) {
					if (nibble_focus_ == 0) {
						nibble_focus_ = 1;
					} else {
						if (cursor_offset_ < bin_doc->size()) {
							cursor_offset_++;
							nibble_focus_ = 0;
						}
					}
				} else {
					if (cursor_offset_ < bin_doc->size()) {
						cursor_offset_++;
					}
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_UP || key == 16) { // Up (Ctrl-P fallback)
				if (cursor_offset_ >= bytes_per_line) {
					cursor_offset_ -= bytes_per_line;
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_DOWN || key == 14) { // Down (Ctrl-N fallback)
				if (cursor_offset_ + bytes_per_line <= bin_doc->size()) {
					cursor_offset_ += bytes_per_line;
				} else if (cursor_offset_ / bytes_per_line < bin_doc->size() / bytes_per_line) {
					cursor_offset_ = bin_doc->size();
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_PPAGE || key == 21) { // Page Up (Ctrl-U)
				size_t diff = lines_fit * bytes_per_line;
				if (cursor_offset_ >= diff) {
					cursor_offset_ -= diff;
				} else {
					cursor_offset_ = 0;
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_NPAGE || key == 22) { // Page Down (Ctrl-V)
				size_t diff = lines_fit * bytes_per_line;
				if (cursor_offset_ + diff <= bin_doc->size()) {
					cursor_offset_ += diff;
				} else {
					cursor_offset_ = bin_doc->size();
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_HOME || key == 1) { // Home (Ctrl-A)
				cursor_offset_ = (cursor_offset_ / bytes_per_line) * bytes_per_line;
				nibble_focus_ = 0;
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_END || key == 5) { // End (Ctrl-E)
				size_t line_start = (cursor_offset_ / bytes_per_line) * bytes_per_line;
				if (bin_doc->size() > 0) {
					cursor_offset_ = std::min(bin_doc->size() - 1, line_start + bytes_per_line - 1);
				} else {
					cursor_offset_ = 0;
				}
				nibble_focus_ = 1;
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == '\t') { // Tab
				hex_focus_ = !hex_focus_;
				nibble_focus_ = 0;
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (key == KEY_BACKSPACE || key == 127 || key == 8) { // Backspace
				if (hex_focus_) {
					if (nibble_focus_ == 1) {
						nibble_focus_ = 0;
					} else {
						if (cursor_offset_ > 0) {
							cursor_offset_--;
							nibble_focus_ = 1;
						}
					}
				} else {
					if (cursor_offset_ > 0) {
						cursor_offset_--;
					}
				}
				bin_doc->break_undo_coalescing();
				redrawn = true;
			} else if (hex_focus_) {
				char c = std::tolower(static_cast<char>(key));
				if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
					uint8_t hex_val = (c >= '0' && c <= '9') ? (c - '0') : (c - 'a' + 10);

					if (cursor_offset_ == bin_doc->size()) {
						bin_doc->append_byte(0x00);
					}

					if (cursor_offset_ < bin_doc->size()) {
						uint8_t val = bin_doc->get_byte(cursor_offset_);
						if (nibble_focus_ == 0) {
							val = (val & 0x0F) | (hex_val << 4);
							bin_doc->set_byte(cursor_offset_, val);
							nibble_focus_ = 1;
						} else {
							val = (val & 0xF0) | hex_val;
							bin_doc->set_byte(cursor_offset_, val);
							if (cursor_offset_ < bin_doc->size()) {
								cursor_offset_++;
							}
							nibble_focus_ = 0;
							bin_doc->break_undo_coalescing();
						}
						redrawn = true;
					}
				}
			} else {
				if (key >= 32 && key <= 126) {
					if (cursor_offset_ == bin_doc->size()) {
						bin_doc->append_byte(0x00);
					}

					if (cursor_offset_ < bin_doc->size()) {
						bin_doc->set_byte(cursor_offset_, static_cast<uint8_t>(key));
						cursor_offset_++;
						bin_doc->break_undo_coalescing();
						redrawn = true;
					}
				}
			}
		}
	}

	if (redrawn) {
		invalidate();
	}
	return redrawn;
}

void hex_editor_window::set_cursor_position() const
{
	size_t bytes_per_line = get_bytes_per_line();
	size_t cursor_line = cursor_offset_ / bytes_per_line;
	size_t cursor_col = cursor_offset_ % bytes_per_line;

	int target_y = y_ + 1 + static_cast<int>(cursor_line - scroll_line_);
	int start_x = x_ + 1;
	int target_x = start_x;

	if (hex_focus_) {
		target_x = start_x + 10 + static_cast<int>(cursor_col * 3 + (cursor_col / 8) + nibble_focus_);
	} else {
		size_t ascii_start_x = start_x + 10 + bytes_per_line * 3 + (bytes_per_line / 8) + 1;
		target_x = static_cast<int>(ascii_start_x + 1 + cursor_col);
	}

	move(target_y, target_x);
}

std::string hex_editor_window::get_status_help() const
{
	auto bin_doc = std::dynamic_pointer_cast<binary_document>(doc_);
	if (!bin_doc) {
		return std::format("Offset: 0x{:08X} ({})  ^T^a^b:Switch", cursor_offset_, cursor_offset_);
	}

	update_highlighter();

	std::string base_help;
	if (cursor_offset_ < bin_doc->size()) {
		uint8_t val = bin_doc->get_byte(cursor_offset_);
		char ascii_char = (val >= 32 && val <= 126) ? static_cast<char>(val) : '.';
		base_help =
		    std::format("Offset: 0x{:08X} ({})  Value: 0x{:02X} ({}, '{}')", cursor_offset_, cursor_offset_, val, val, ascii_char);
	} else {
		base_help = std::format("Offset: 0x{:08X} ({})  Value: --", cursor_offset_, cursor_offset_);
	}

	std::string extra_help;
	if (highlighter_ && cursor_offset_ < bin_doc->size()) {
		highlight_info info = highlighter_->get_info(bin_doc->get_data(), cursor_offset_);
		if (!info.description.empty()) {
			extra_help = " | " + info.description;
		}
	}

	std::string suffix = "  ^T^a^b:Switch";
	int max_len = COLS - 2 - static_cast<int>(suffix.length());
	if (max_len < 20)
		max_len = 20;

	std::string combined = base_help + extra_help;
	if (static_cast<int>(combined.length()) > max_len) {
		combined = combined.substr(0, max_len - 3) + "...";
	}

	return combined + suffix;
}

size_t hex_editor_window::get_bytes_per_line() const
{
	int content_w = width_ - 2;
	if (content_w <= 15)
		return 16;
	int max_b = (content_w - 15) * 8 / 33;
	int b = (max_b / 16) * 16;
	if (b < 16)
		b = 16;
	return static_cast<size_t>(b);
}

void hex_editor_window::update_highlighter() const
{
	auto bin_doc = std::dynamic_pointer_cast<binary_document>(doc_);
	if (!bin_doc)
		return;

	size_t current_rev = bin_doc->get_revision();
	if (current_rev != last_highlighter_revision_) {
		last_highlighter_revision_ = current_rev;
		highlighter_ = hex_highlighter_registry::get_instance().detect_highlighter(bin_doc->get_data());
		if (highlighter_) {
			highlighter_->parse(bin_doc->get_data());
		}
	}
}

int hex_editor_window::get_color_pair_for_byte(size_t offset, uint8_t val, const std::shared_ptr<binary_document> &bin_doc) const
{
	update_highlighter();
	int pair = 3;
	if (highlighter_) {
		highlight_info info = highlighter_->get_info(bin_doc->get_data(), offset);
		switch (info.type) {
			case hex_semantic_type::magic:
				pair = 30;
				break;
			case hex_semantic_type::file_header:
				pair = 12;
				break;
			case hex_semantic_type::prog_header:
				pair = 32;
				break;
			case hex_semantic_type::sect_header:
				pair = 23;
				break;
			case hex_semantic_type::code_section:
				pair = 30;
				break;
			case hex_semantic_type::data_section:
				pair = 4;
				break;
			case hex_semantic_type::rodata_section:
				pair = 3;
				break;
			case hex_semantic_type::symtab_section:
				pair = 9;
				break;
			default:
				if (val == 0)
					pair = 9;
				else if (val < 32 || val > 126)
					pair = 4;
				break;
		}
	} else {
		if (val == 0) {
			pair = 9;
		} else if (val < 32 || val > 126) {
			pair = 4;
		}
	}
	return pair;
}
