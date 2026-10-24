#include "document.h"
#include <algorithm>
#include <format>
#include <map>
#include "event_logger.h"
#include "utf8.h"

bool document::has_multi_cursor() const noexcept
{
	std::shared_lock lock(mutex_);
	return !multi_cursors_.empty();
}

std::vector<multi_caret> document::get_multi_cursors() const
{
	std::shared_lock lock(mutex_);
	return multi_cursors_;
}

void document::clear_multi_cursor()
{
	std::unique_lock lock(mutex_);
	if (!multi_cursors_.empty() || !multi_cursor_word_.empty()) {
		multi_cursors_.clear();
		multi_cursor_word_.clear();
		lock.unlock();
		request_redraw();
	}
}

std::vector<text_range> document::get_scope_occurrences_unlocked() const
{
	std::string target = multi_cursor_word_.empty() ? get_word_under_cursor_unlocked() : multi_cursor_word_;
	if (target.empty()) {
		return {};
	}

	// 1. If we have active LSP highlights for this exact word, use them
	if (!lsp_highlights_.empty()) {
		bool lsp_matches = true;
		for (const auto &hl : lsp_highlights_) {
			if (hl.start_y >= 0 && hl.start_y < static_cast<int>(lines_.size())) {
				std::string line_text = lines_[hl.start_y]->get_text();
				size_t start_byte = lines_[hl.start_y]->char_to_byte_offset(hl.start_x);
				size_t end_byte = lines_[hl.start_y]->char_to_byte_offset(hl.end_x);
				if (end_byte >= start_byte && end_byte <= line_text.size()) {
					if (line_text.substr(start_byte, end_byte - start_byte) != target) {
						lsp_matches = false;
						break;
					}
				}
			}
		}
		if (lsp_matches) {
			return lsp_highlights_;
		}
	}

	// 2. Heuristic scope scanner fallback
	int start_y = 0;
	int end_y = static_cast<int>(lines_.size()) - 1;

	if (enclosing_scope_.has_value() && enclosing_scope_->end_y > enclosing_scope_->start_y) {
		start_y = std::max(0, enclosing_scope_->start_y);
		end_y = std::min(static_cast<int>(lines_.size()) - 1, enclosing_scope_->end_y);
	}

	std::vector<text_range> results;
	for (int y = start_y; y <= end_y; ++y) {
		const auto &l = lines_[y];
		int len_chars = l->length_in_chars();
		int char_pos = 0;
		while (char_pos < len_chars) {
			word_span span = l->get_word_span_at(char_pos, "_");
			if (!span.empty() && span.kind == span_kind::word) {
				if (span.word == target) {
					results.push_back({
					    .start_y = y,
					    .start_x = span.start_char_pos,
					    .end_y = y,
					    .end_x = span.end_char_pos,
					});
				}
				char_pos = std::max(char_pos + 1, span.end_char_pos);
			} else {
				++char_pos;
			}
		}
	}
	return results;
}

void document::select_all_scope_highlights()
{
	std::unique_lock lock(mutex_);
	std::string word = multi_cursor_word_.empty() ? get_word_under_cursor_unlocked() : multi_cursor_word_;
	if (word.empty()) {
		return;
	}
	multi_cursor_word_ = word;

	auto occs = get_scope_occurrences_unlocked();
	if (occs.empty()) {
		return;
	}

	// Compute relative offset of cursor within the current occurrence
	int rel_offset = 0;
	bool found_current = false;
	for (const auto &occ : occs) {
		if (occ.start_y == cursor_y_ && cursor_x_ >= occ.start_x && cursor_x_ <= occ.end_x) {
			rel_offset = cursor_x_ - occ.start_x;
			found_current = true;
			break;
		}
	}
	if (!found_current && !occs.empty()) {
		rel_offset = 0;
	}

	bool user_had_selection = has_selection_unlocked();

	multi_cursors_.clear();
	for (const auto &occ : occs) {
		multi_cursors_.push_back({
		    .line = occ.start_y,
		    .col = occ.start_x + rel_offset,
		    .sel_start = user_had_selection ? occ.start_x : -1,
		    .sel_end = user_had_selection ? occ.end_x : -1,
		});
	}

	std::sort(multi_cursors_.begin(), multi_cursors_.end(), [](const multi_caret &a, const multi_caret &b) {
		if (a.line != b.line) {
			return a.line < b.line;
		}
		return a.col < b.col;
	});

	std::string msg = std::format("Multi-cursor: {} occurrences in scope (Esc to cancel)", multi_cursors_.size());
	global_queue_.push(editor_event{.type = event_type::set_transient_status, .payload = msg});
	lock.unlock();
	request_redraw();
}

void document::select_next_scope_highlight()
{
	std::unique_lock lock(mutex_);
	if (multi_cursors_.empty()) {
		std::string word = get_word_under_cursor_unlocked();
		if (word.empty()) {
			return;
		}
		multi_cursor_word_ = word;
		auto occs = get_scope_occurrences_unlocked();
		if (occs.empty()) {
			return;
		}

		// Find occurrence covering current cursor
		size_t match_idx = 0;
		int rel_offset = 0;
		for (size_t i = 0; i < occs.size(); ++i) {
			if (occs[i].start_y == cursor_y_ && cursor_x_ >= occs[i].start_x && cursor_x_ <= occs[i].end_x) {
				match_idx = i;
				rel_offset = cursor_x_ - occs[i].start_x;
				break;
			}
		}

		bool user_had_selection = has_selection_unlocked();
		const auto &first = occs[match_idx];
		multi_cursors_.push_back({
		    .line = first.start_y,
		    .col = first.start_x + rel_offset,
		    .sel_start = user_had_selection ? first.start_x : -1,
		    .sel_end = user_had_selection ? first.end_x : -1,
		});

		std::string msg = std::format("Multi-cursor: 1/{} selected (Ctrl+D for next, Esc to cancel)", occs.size());
		global_queue_.push(editor_event{.type = event_type::set_transient_status, .payload = msg});
		lock.unlock();
		request_redraw();
		return;
	}

	auto occs = get_scope_occurrences_unlocked();
	if (occs.empty()) {
		return;
	}

	if (multi_cursors_.size() >= occs.size()) {
		std::string msg = std::format("Multi-cursor: all {} occurrences selected in scope", occs.size());
		global_queue_.push(editor_event{.type = event_type::set_transient_status, .payload = msg});
		return;
	}

	// Compute relative offset from the first caret
	int rel_offset = 0;
	if (!multi_cursors_.empty()) {
		for (const auto &occ : occs) {
			if (occ.start_y == multi_cursors_.front().line) {
				rel_offset = multi_cursors_.front().col - occ.start_x;
				break;
			}
		}
	}

	// Find the occurrence after the last selected caret in file order
	const auto &last_caret = multi_cursors_.back();
	int best_next_idx = -1;

	for (size_t i = 0; i < occs.size(); ++i) {
		const auto &occ = occs[i];
		bool after_last = (occ.start_y > last_caret.line) ||
				  (occ.start_y == last_caret.line && occ.start_x > last_caret.col);
		if (after_last) {
			bool already_selected = false;
			for (const auto &c : multi_cursors_) {
				if (c.line == occ.start_y && (c.col == occ.start_x + rel_offset || (c.has_selection() && c.sel_start == occ.start_x))) {
					already_selected = true;
					break;
				}
			}
			if (!already_selected) {
				best_next_idx = static_cast<int>(i);
				break;
			}
		}
	}

	// If not found after last, wrap around to the top
	if (best_next_idx == -1) {
		for (size_t i = 0; i < occs.size(); ++i) {
			const auto &occ = occs[i];
			bool already_selected = false;
			for (const auto &c : multi_cursors_) {
				if (c.line == occ.start_y && (c.col == occ.start_x + rel_offset || (c.has_selection() && c.sel_start == occ.start_x))) {
					already_selected = true;
					break;
				}
			}
			if (!already_selected) {
				best_next_idx = static_cast<int>(i);
				break;
			}
		}
	}

	if (best_next_idx != -1) {
		const auto &next_occ = occs[best_next_idx];
		bool user_had_selection = has_selection_unlocked();
		multi_cursors_.push_back({
		    .line = next_occ.start_y,
		    .col = next_occ.start_x + rel_offset,
		    .sel_start = user_had_selection ? next_occ.start_x : -1,
		    .sel_end = user_had_selection ? next_occ.end_x : -1,
		});

		std::sort(multi_cursors_.begin(), multi_cursors_.end(), [](const multi_caret &a, const multi_caret &b) {
			if (a.line != b.line) {
				return a.line < b.line;
			}
			return a.col < b.col;
		});

		cursor_y_ = next_occ.start_y;
		cursor_x_ = next_occ.start_x + rel_offset;

		std::string msg = std::format("Multi-cursor: {}/{} selected (Ctrl+D for next, Esc to cancel)",
					      multi_cursors_.size(), occs.size());
		global_queue_.push(editor_event{.type = event_type::set_transient_status, .payload = msg});
		lock.unlock();
		request_redraw();
	}
}

void document::insert_char_multi_cursor(std::string_view utf8_char)
{
	std::unique_lock lock(mutex_);
	if (is_read_only() || multi_cursors_.empty()) {
		return;
	}

	begin_edit_group("Multi-cursor typing", undo_group_type::typing);

	// Group carets by line: line -> vector of caret indices in multi_cursors_
	std::map<int, std::vector<size_t>> line_carets;
	for (size_t i = 0; i < multi_cursors_.size(); ++i) {
		line_carets[multi_cursors_[i].line].push_back(i);
	}

	for (auto &[y, indices] : line_carets) {
		if (y < 0 || y >= line_count_unlocked()) {
			continue;
		}

		// Sort carets on this line ascending by column position
		std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
			int pos_a = multi_cursors_[a].has_selection() ? multi_cursors_[a].sel_start : multi_cursors_[a].col;
			int pos_b = multi_cursors_[b].has_selection() ? multi_cursors_[b].sel_start : multi_cursors_[b].col;
			return pos_a < pos_b;
		});

		record_action(edit_action::action_type::replace_line, y, lines_[y]);

		int running_shift = 0;
		for (size_t idx : indices) {
			auto &mc = multi_cursors_[idx];
			if (mc.has_selection()) {
				int actual_start = mc.sel_start + running_shift;
				int actual_end = mc.sel_end + running_shift;
				int del_len = actual_end - actual_start;
				for (int k = 0; k < del_len; ++k) {
					lines_[y]->remove_at(actual_start);
				}
				lines_[y]->insert_at(actual_start, utf8_char);
				mc.col = actual_start + 1;
				mc.sel_start = -1;
				mc.sel_end = -1;
				running_shift += (1 - del_len);
			} else {
				int actual_col = mc.col + running_shift;
				lines_[y]->insert_at(actual_col, utf8_char);
				mc.col = actual_col + 1;
				mc.sel_start = -1;
				mc.sel_end = -1;
				running_shift += 1;
			}
		}
		mark_line_dirty(lines_[y]);
	}

	// Re-sort multi_cursors_ ascending
	std::sort(multi_cursors_.begin(), multi_cursors_.end(), [](const multi_caret &a, const multi_caret &b) {
		if (a.line != b.line) {
			return a.line < b.line;
		}
		return a.col < b.col;
	});

	bool found_current = false;
	for (const auto &mc : multi_cursors_) {
		if (mc.line == cursor_y_) {
			cursor_x_ = mc.col;
			found_current = true;
			break;
		}
	}
	if (!found_current && !multi_cursors_.empty()) {
		cursor_y_ = multi_cursors_.front().line;
		cursor_x_ = multi_cursors_.front().col;
	}

	set_modified();
	end_edit_group();
	update_target_cursor_x_unlocked();
	lock.unlock();
	request_redraw();
}

void document::backspace_multi_cursor()
{
	std::unique_lock lock(mutex_);
	if (is_read_only() || multi_cursors_.empty()) {
		return;
	}

	begin_edit_group("Multi-cursor backspace", undo_group_type::backspace);

	std::map<int, std::vector<size_t>> line_carets;
	for (size_t i = 0; i < multi_cursors_.size(); ++i) {
		line_carets[multi_cursors_[i].line].push_back(i);
	}

	for (auto &[y, indices] : line_carets) {
		if (y < 0 || y >= line_count_unlocked()) {
			continue;
		}

		std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
			int pos_a = multi_cursors_[a].has_selection() ? multi_cursors_[a].sel_start : multi_cursors_[a].col;
			int pos_b = multi_cursors_[b].has_selection() ? multi_cursors_[b].sel_start : multi_cursors_[b].col;
			return pos_a < pos_b;
		});

		record_action(edit_action::action_type::replace_line, y, lines_[y]);

		int running_shift = 0;
		for (size_t idx : indices) {
			auto &mc = multi_cursors_[idx];
			if (mc.has_selection()) {
				int actual_start = mc.sel_start + running_shift;
				int actual_end = mc.sel_end + running_shift;
				int del_len = actual_end - actual_start;
				for (int k = 0; k < del_len; ++k) {
					lines_[y]->remove_at(actual_start);
				}
				mc.col = actual_start;
				mc.sel_start = -1;
				mc.sel_end = -1;
				running_shift -= del_len;
			} else {
				int actual_col = mc.col + running_shift;
				if (actual_col > 0) {
					lines_[y]->remove_at(actual_col - 1);
					mc.col = actual_col - 1;
					mc.sel_start = -1;
					mc.sel_end = -1;
					running_shift -= 1;
				} else {
					mc.col = 0;
					mc.sel_start = -1;
					mc.sel_end = -1;
				}
			}
		}
		mark_line_dirty(lines_[y]);
	}

	std::sort(multi_cursors_.begin(), multi_cursors_.end(), [](const multi_caret &a, const multi_caret &b) {
		if (a.line != b.line) {
			return a.line < b.line;
		}
		return a.col < b.col;
	});

	bool found_current = false;
	for (const auto &mc : multi_cursors_) {
		if (mc.line == cursor_y_) {
			cursor_x_ = mc.col;
			found_current = true;
			break;
		}
	}
	if (!found_current && !multi_cursors_.empty()) {
		cursor_y_ = multi_cursors_.front().line;
		cursor_x_ = multi_cursors_.front().col;
	}

	set_modified();
	end_edit_group();
	update_target_cursor_x_unlocked();
	lock.unlock();
	request_redraw();
}

void document::delete_char_multi_cursor()
{
	std::unique_lock lock(mutex_);
	if (is_read_only() || multi_cursors_.empty()) {
		return;
	}

	begin_edit_group("Multi-cursor delete", undo_group_type::backspace);

	std::map<int, std::vector<size_t>> line_carets;
	for (size_t i = 0; i < multi_cursors_.size(); ++i) {
		line_carets[multi_cursors_[i].line].push_back(i);
	}

	for (auto &[y, indices] : line_carets) {
		if (y < 0 || y >= line_count_unlocked()) {
			continue;
		}

		std::sort(indices.begin(), indices.end(), [this](size_t a, size_t b) {
			int pos_a = multi_cursors_[a].has_selection() ? multi_cursors_[a].sel_start : multi_cursors_[a].col;
			int pos_b = multi_cursors_[b].has_selection() ? multi_cursors_[b].sel_start : multi_cursors_[b].col;
			return pos_a < pos_b;
		});

		record_action(edit_action::action_type::replace_line, y, lines_[y]);

		int running_shift = 0;
		for (size_t idx : indices) {
			auto &mc = multi_cursors_[idx];
			if (mc.has_selection()) {
				int actual_start = mc.sel_start + running_shift;
				int actual_end = mc.sel_end + running_shift;
				int del_len = actual_end - actual_start;
				for (int k = 0; k < del_len; ++k) {
					lines_[y]->remove_at(actual_start);
				}
				mc.col = actual_start;
				mc.sel_start = -1;
				mc.sel_end = -1;
				running_shift -= del_len;
			} else {
				int actual_col = mc.col + running_shift;
				if (actual_col < lines_[y]->length_in_chars()) {
					lines_[y]->remove_at(actual_col);
					mc.col = actual_col;
					mc.sel_start = -1;
					mc.sel_end = -1;
					running_shift -= 1;
				} else {
					mc.col = actual_col;
					mc.sel_start = -1;
					mc.sel_end = -1;
				}
			}
		}
		mark_line_dirty(lines_[y]);
	}

	std::sort(multi_cursors_.begin(), multi_cursors_.end(), [](const multi_caret &a, const multi_caret &b) {
		if (a.line != b.line) {
			return a.line < b.line;
		}
		return a.col < b.col;
	});

	bool found_current = false;
	for (const auto &mc : multi_cursors_) {
		if (mc.line == cursor_y_) {
			cursor_x_ = mc.col;
			found_current = true;
			break;
		}
	}
	if (!found_current && !multi_cursors_.empty()) {
		cursor_y_ = multi_cursors_.front().line;
		cursor_x_ = multi_cursors_.front().col;
	}

	set_modified();
	end_edit_group();
	update_target_cursor_x_unlocked();
	lock.unlock();
	request_redraw();
}

void document::move_multi_cursor(int dx)
{
	std::unique_lock lock(mutex_);
	for (auto &mc : multi_cursors_) {
		if (dx < 0) {
			if (mc.has_selection()) {
				mc.col = mc.sel_start;
				mc.sel_start = mc.sel_end = -1;
			} else if (mc.col > 0) {
				mc.col -= 1;
			}
		} else if (dx > 0) {
			if (mc.has_selection()) {
				mc.col = mc.sel_end;
				mc.sel_start = mc.sel_end = -1;
			} else {
				mc.col += 1;
			}
		}
	}
	bool found_current = false;
	for (const auto &mc : multi_cursors_) {
		if (mc.line == cursor_y_) {
			cursor_x_ = mc.col;
			found_current = true;
			break;
		}
	}
	if (!found_current && !multi_cursors_.empty()) {
		cursor_y_ = multi_cursors_.front().line;
		cursor_x_ = multi_cursors_.front().col;
	}
	update_target_cursor_x_unlocked();
	lock.unlock();
	request_redraw();
}
