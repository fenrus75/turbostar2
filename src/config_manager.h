#pragma once

#include <string>

/**
 * @brief Manages global application configuration, persisting to ~/.turbostar.
 */
class config_manager
{
      public:
	static config_manager &get_instance();

	void load();
	void save();

	std::string get_clang_format_style() const { return clang_format_style_; }
	void set_clang_format_style(const std::string &style) { clang_format_style_ = style; }

	std::string get_build_system() const { return build_system_; }
	void set_build_system(const std::string &sys) { build_system_ = sys; }

	std::string get_build_directory() const { return build_directory_; }
	void set_build_directory(const std::string &dir) { build_directory_ = dir; }

      private:
	config_manager() = default;
	std::string get_config_file_path() const;

	std::string clang_format_style_{"file"};
	std::string build_system_{"meson"};
	std::string build_directory_{"build"};
};
