#pragma once
#include "ui/ui_element.h"
#include <filesystem>
#include <vector>
#include <chrono>
#include <optional>
#include <functional>

namespace fs = std::filesystem;

class ui_fileselector;

class ui_file_info_panel : public ui_element
{
      public:
        ui_file_info_panel(int x, int y, int width, ui_fileselector* fs_view);

        void draw(int abs_x, int abs_y) const override;
        bool handle_event(const editor_event &/*ev*/, int /*abs_x*/, int /*abs_y*/) override { return false; }

      private:
        ui_fileselector* fs_view_;
};

struct file_entry {
        fs::path path;
        std::string display_name;
        bool is_dir;
        uintmax_t size;
        std::chrono::system_clock::time_point mtime;
};

class ui_fileselector : public ui_element
{
      public:
        ui_fileselector(std::string name, int x, int y, int width, int height, 
                                        const std::string& initial_path,
                                        std::function<void(const std::string&)> on_selection_changed,
                                        std::function<void(const std::string&)> on_submit);

        void draw(int abs_x, int abs_y) const override;
        bool is_focusable() const override { return true; }
        bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;

        void populate_files();
        void set_current_path(const fs::path& path);
        fs::path get_current_path() const { return current_path_; }

        std::optional<file_entry> get_selected_entry() const;
        std::string get_autocomplete_suggestion(const std::string& buffer) const;

      private:
        fs::path current_path_;
        std::vector<file_entry> files_;
        int selected_index_{0};
        int scroll_top_{0};

        std::function<void(const std::string&)> on_selection_changed_;
        std::function<void(const std::string&)> on_submit_;
};
