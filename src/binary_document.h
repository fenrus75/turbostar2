#pragma once

#include <cstdint>
#include <deque>
#include <vector>
#include "document.h"

struct binary_edit_action {
	size_t offset;
	uint8_t old_val;
	uint8_t new_val;
	bool is_append;
};

struct binary_action_group {
	std::vector<binary_edit_action> actions;
};

class binary_document : public document
{
      public:
	binary_document(event_queue &global_queue);
	binary_document(event_queue &global_queue, const std::string &filename);
	~binary_document() override = default;

	bool load_from_file(const std::string &filename) override;
	bool save() override;
	bool save_to_file(const std::string &filename) override;

	void undo() override;
	void redo() override;
	void break_undo_coalescing() override;
	size_t get_undo_count() const override;

	// Custom Binary-specific operations
	size_t size() const;
	uint8_t get_byte(size_t offset) const;
	void set_byte(size_t offset, uint8_t val);
	void append_byte(uint8_t val);

      private:
	void record_byte_edit(size_t offset, uint8_t old_val, uint8_t new_val, bool is_append);

	std::vector<uint8_t> data_;

	std::deque<binary_action_group> binary_undo_stack_;
	std::deque<binary_action_group> binary_redo_stack_;
	binary_action_group binary_current_group_;
};
