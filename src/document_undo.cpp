#include "document.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include "event_logger.h"
#include "config_manager.h"
#include "git_manager.h"
#include "highlighter_registry.h"
#include "clangd_manager.h"
#include "fs_utils.h"

namespace fs = std::filesystem;


void document::begin_edit_group()
{
	if (!is_recording_actions_)
		return;
	if (edit_group_depth_ == 0) {
		current_action_group_.actions.clear();
		current_action_group_.cursor_y_before = cursor_y_;
		current_action_group_.cursor_x_before = cursor_x_;
	}
	edit_group_depth_++;
}


void document::end_edit_group()
{
	if (!is_recording_actions_)
		return;
	edit_group_depth_--;
	if (edit_group_depth_ == 0 && !current_action_group_.empty()) {
		current_action_group_.cursor_y_after = cursor_y_;
		current_action_group_.cursor_x_after = cursor_x_;
		undo_stack_.push_back(current_action_group_);
		if (undo_stack_.size() > max_undo_steps_) {
			undo_stack_.pop_front();
		}
		redo_stack_.clear();
		current_action_group_.actions.clear();
	}
}


void document::record_action(edit_action::action_type type, int y, std::shared_ptr<line> saved_line)
{
	if (!is_recording_actions_)
		return;

	edit_action act;
	act.type = type;
	act.y = y;
	if (saved_line) {
		act.saved_line = std::make_shared<line>(*saved_line);
	}

	current_action_group_.actions.push_back(act);
}


void document::undo()
{
	std::unique_lock lock(mutex_);
	if (undo_stack_.empty())
		return;

	is_recording_actions_ = false;
	action_group group = undo_stack_.back();
	undo_stack_.pop_back();

	action_group inverse_group;
	inverse_group.cursor_y_before = group.cursor_y_after;
	inverse_group.cursor_x_before = group.cursor_x_after;
	inverse_group.cursor_y_after = group.cursor_y_before;
	inverse_group.cursor_x_after = group.cursor_x_before;

	// Process actions in reverse order
	for (auto it = group.actions.rbegin(); it != group.actions.rend(); ++it) {
		const auto &act = *it;
		edit_action inverse_act;
		inverse_act.y = act.y;

		if (act.type == edit_action::action_type::replace_line) {
			inverse_act.type = edit_action::action_type::replace_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_[act.y] = std::make_shared<line>(*act.saved_line);
			mark_line_dirty(lines_[act.y]);
		} else if (act.type == edit_action::action_type::insert_line) {
			inverse_act.type = edit_action::action_type::delete_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_.erase(lines_.begin() + act.y);
			adjust_selection_for_line_delete(act.y);
		} else if (act.type == edit_action::action_type::delete_line) {
			inverse_act.type = edit_action::action_type::insert_line;
			lines_.insert(lines_.begin() + act.y, std::make_shared<line>(*act.saved_line));
			mark_line_dirty(lines_[act.y]);
		}

		inverse_group.actions.push_back(inverse_act);
	}

	cursor_y_ = group.cursor_y_before;
	cursor_x_ = group.cursor_x_before;

	std::reverse(inverse_group.actions.begin(), inverse_group.actions.end());
	redo_stack_.push_back(inverse_group);
	
	set_modified();
	is_recording_actions_ = true;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}


void document::redo()
{
	std::unique_lock lock(mutex_);
	if (redo_stack_.empty())
		return;

	is_recording_actions_ = false;
	action_group group = redo_stack_.back();
	redo_stack_.pop_back();

	action_group inverse_group;
	inverse_group.cursor_y_before = group.cursor_y_after;
	inverse_group.cursor_x_before = group.cursor_x_after;
	inverse_group.cursor_y_after = group.cursor_y_before;
	inverse_group.cursor_x_after = group.cursor_x_before;

	for (auto it = group.actions.rbegin(); it != group.actions.rend(); ++it) {
		const auto &act = *it;
		edit_action inverse_act;
		inverse_act.y = act.y;

		if (act.type == edit_action::action_type::replace_line) {
			inverse_act.type = edit_action::action_type::replace_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_[act.y] = std::make_shared<line>(*act.saved_line);
			mark_line_dirty(lines_[act.y]);
		} else if (act.type == edit_action::action_type::insert_line) {
			inverse_act.type = edit_action::action_type::delete_line;
			inverse_act.saved_line = std::make_shared<line>(*lines_[act.y]);
			lines_.erase(lines_.begin() + act.y);
			adjust_selection_for_line_delete(act.y);
		} else if (act.type == edit_action::action_type::delete_line) {
			inverse_act.type = edit_action::action_type::insert_line;
			lines_.insert(lines_.begin() + act.y, std::make_shared<line>(*act.saved_line));
			mark_line_dirty(lines_[act.y]);
		}

		inverse_group.actions.push_back(inverse_act);
	}

	cursor_y_ = group.cursor_y_before;
	cursor_x_ = group.cursor_x_before;

	std::reverse(inverse_group.actions.begin(), inverse_group.actions.end());
	undo_stack_.push_back(inverse_group);

	set_modified();
	is_recording_actions_ = true;
	target_cursor_x_ = cursor_x_;
	lock.unlock();
	notify_cursor_changed();
}
