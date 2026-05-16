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

      private:
	config_manager() = default;
	std::string get_config_file_path() const;

	std::string clang_format_style_{"file"};
};
