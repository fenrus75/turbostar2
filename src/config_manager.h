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

	bool is_lsp_enabled() const { return lsp_enabled_; }
	void set_lsp_enabled(bool enabled) { lsp_enabled_ = enabled; }

	bool is_auto_open_error_files() const { return auto_open_error_files_; }
	void set_auto_open_error_files(bool auto_open) { auto_open_error_files_ = auto_open; }

	bool is_compile_on_save() const { return compile_on_save_; }
	void set_compile_on_save(bool compile) { compile_on_save_ = compile; }

	std::string get_llm_url() const { return llm_url_; }
	void set_llm_url(const std::string &url) { llm_url_ = url; }

      private:
	config_manager() = default;
	std::string get_config_file_path() const;

	std::string clang_format_style_{"file"};
	std::string build_system_{"meson"};
	std::string build_directory_{"build"};
	std::string llm_url_{"http://192.168.1.42:8080"};
	bool lsp_enabled_{true};
	bool auto_open_error_files_{true};
	bool compile_on_save_{false};
};
