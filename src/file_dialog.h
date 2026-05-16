#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include "dialog.h"

namespace fs = std::filesystem;

enum class file_dialog_mode { open, save };

enum class focus_element {
	entry_box,
	history_btn,
	file_view,
	ok_btn,
	cancel_btn
};

struct file_entry {
	fs::path path;
	std::string display_name;
	bool is_dir;
	uintmax_t size;
	std::chrono::system_clock::time_point mtime;
};

class file_dialog : public dialog
{
      public:
	file_dialog(const std::string &title, file_dialog_mode mode,
		    bool autocomplete, const std::string &initial_path);
	~file_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	std::string get_result() const override;

      private:
	void populate_files();
	std::string get_autocomplete_suggestion() const;
	void draw_button(int y, int x, const std::string &text, char hotkey,
			 bool focused) const;

	file_dialog_mode mode_;
	bool autocomplete_;
	fs::path current_path_;
	std::string filename_buffer_;

	std::vector<file_entry> files_;
	int selected_index_{0};
	int scroll_top_{0}; // refers to row offset, or maybe file index offset
	focus_element focus_{focus_element::entry_box};

	// History dropdown state
	std::vector<std::string> file_history_;
	bool history_dropdown_open_{false};
	int history_sel_idx_{0};
};
