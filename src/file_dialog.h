#pragma once

#include "dialog.h"
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

enum class file_dialog_mode { open, save };

class file_dialog : public dialog {
public:
	file_dialog(const std::string& title, file_dialog_mode mode, const std::string& initial_path);
	~file_dialog() override = default;

	void draw() const override;
	dialog_result handle_key(int key) override;
	std::string get_result() const override;

private:
	void populate_files();

	file_dialog_mode mode_;
	fs::path current_path_;
	std::string filename_buffer_;
	std::vector<fs::path> files_;
	int selected_index_{0};
	int scroll_top_{0};
	int focus_idx_{0}; // 0: filename, 1: filelist, 2: open/save, 3: cancel
};
