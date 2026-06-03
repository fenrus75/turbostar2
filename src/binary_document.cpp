#include "binary_document.h"
#include <filesystem>
#include <fstream>
#include "event_logger.h"
#include "fs_utils.h"
#include "git_manager.h"

namespace fs = std::filesystem;

binary_document::binary_document(event_queue &global_queue) : document(global_queue)
{
}

binary_document::binary_document(event_queue &global_queue, const std::string &filename) : document(global_queue)
{
	load_from_file(filename);
}

bool binary_document::load_from_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);

	std::error_code ec;
	auto size = fs::file_size(filename, ec);
	if (ec) {
		event_logger::get_instance().log("Load failed: Could not determine file size of {}", filename);
		return false;
	}

	// 50MB Limit
	if (size > 50 * 1024 * 1024) {
		event_logger::get_instance().log("Load failed: File {} exceeds 50MB safety limit (size: {} bytes)", filename, size);
		return false;
	}

	std::ifstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		event_logger::get_instance().log("Load failed: Could not open file {} in binary mode", filename);
		return false;
	}

	data_.resize(size);
	if (size > 0) {
		file.read(reinterpret_cast<char *>(data_.data()), size);
	}

	filename_ = filename;
	safe_filename_ = fs_utils::safe_absolute(filename_).string();
	modified_ = false;

	auto mtime = fs::last_write_time(filename_, ec);
	if (!ec) {
		last_disk_mtime_ = mtime;
		has_last_disk_mtime_ = true;
	} else {
		has_last_disk_mtime_ = false;
	}

	binary_undo_stack_.clear();
	binary_redo_stack_.clear();
	binary_current_group_.actions.clear();

	return true;
}

bool binary_document::save()
{
	std::shared_lock lock(mutex_);
	std::string fname = filename_;
	bool modified = modified_;
	lock.unlock();

	if (fname.empty()) {
		event_logger::get_instance().log("Save failed: No filename specified for binary document.");
		return false;
	}

	if (!modified) {
		git_manager::get_instance().request_status(fname);
		return true;
	}

	return save_to_file(fname);
}

bool binary_document::save_to_file(const std::string &filename)
{
	std::unique_lock lock(mutex_);
	break_undo_coalescing_unlocked();
	lock.unlock();

	// Backup file: rename filename to filename + "~"
	if (fs::exists(filename)) {
		std::error_code ec;
		fs::rename(filename, filename + "~", ec);
		if (ec) {
			event_logger::get_instance().log("Binary backup rename failed: {}", ec.message());
		}
	}

	std::ofstream file(filename, std::ios::binary);
	if (!file.is_open()) {
		event_logger::get_instance().log("Save failed: Could not open file {} for writing binary data", filename);
		return false;
	}

	if (!data_.empty()) {
		file.write(reinterpret_cast<const char *>(data_.data()), data_.size());
	}

	std::unique_lock lock_after(mutex_);
	modified_ = false;
	filename_ = filename;
	safe_filename_ = fs_utils::safe_absolute(filename_).string();

	std::error_code ec;
	auto mtime = fs::last_write_time(filename_, ec);
	if (!ec) {
		last_disk_mtime_ = mtime;
		has_last_disk_mtime_ = true;
	}
	lock_after.unlock();

	git_manager::get_instance().request_status(filename);

	return true;
}

void binary_document::undo()
{
	break_undo_coalescing();
	if (binary_undo_stack_.empty())
		return;

	auto group = binary_undo_stack_.back();
	binary_undo_stack_.pop_back();

	// Apply actions in reverse order
	for (auto it = group.actions.rbegin(); it != group.actions.rend(); ++it) {
		if (it->is_append) {
			if (!data_.empty()) {
				data_.pop_back();
			}
		} else {
			if (it->offset < data_.size()) {
				data_[it->offset] = it->old_val;
			}
		}
	}

	binary_redo_stack_.push_back(group);
	modified_ = true;
	notify_undo_changed_event();
}

void binary_document::redo()
{
	if (binary_redo_stack_.empty())
		return;

	auto group = binary_redo_stack_.back();
	binary_redo_stack_.pop_back();

	// Apply actions in forward order
	for (const auto &act : group.actions) {
		if (act.is_append) {
			data_.push_back(act.new_val);
		} else {
			if (act.offset < data_.size()) {
				data_[act.offset] = act.new_val;
			}
		}
	}

	binary_undo_stack_.push_back(group);
	modified_ = true;
	notify_undo_changed_event();
}

void binary_document::break_undo_coalescing()
{
	if (!binary_current_group_.actions.empty()) {
		binary_undo_stack_.push_back(binary_current_group_);
		binary_current_group_.actions.clear();
		binary_redo_stack_.clear();
	}
}

size_t binary_document::get_undo_count() const
{
	return binary_undo_stack_.size();
}

size_t binary_document::size() const
{
	return data_.size();
}

uint8_t binary_document::get_byte(size_t offset) const
{
	if (offset >= data_.size())
		return 0;
	return data_[offset];
}

void binary_document::set_byte(size_t offset, uint8_t val)
{
	if (offset >= data_.size())
		return;
	uint8_t old_val = data_[offset];
	if (old_val == val)
		return;

	data_[offset] = val;
	record_byte_edit(offset, old_val, val, false);
}

void binary_document::append_byte(uint8_t val)
{
	size_t offset = data_.size();
	data_.push_back(val);
	record_byte_edit(offset, 0, val, true);
}

void binary_document::record_byte_edit(size_t offset, uint8_t old_val, uint8_t new_val, bool is_append)
{
	binary_edit_action act{offset, old_val, new_val, is_append};
	binary_current_group_.actions.push_back(act);
	modified_ = true;
}
