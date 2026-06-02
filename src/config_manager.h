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
	void load_from_file(const std::string &path);
	void save_global();
	void save_project(const std::string &project_root);
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

	bool is_software_map_enabled() const { return software_map_enabled_; }
	void set_software_map_enabled(bool enabled) { software_map_enabled_ = enabled; }

	std::string get_default_model_id() const { return default_model_id_; }
	void set_default_model_id(const std::string &id) { default_model_id_ = id; }

	bool is_paranoid_mode() const { return paranoid_mode_; }
	void set_paranoid_mode(bool paranoid) { paranoid_mode_ = paranoid; }

	bool is_log_all_tool_calls() const { return log_all_tool_calls_; }
	void set_log_all_tool_calls(bool log_all) { log_all_tool_calls_ = log_all; }

	std::string get_main_executable() const { return main_executable_; }
	void set_main_executable(const std::string &exe) { main_executable_ = exe; }

	std::string get_run_arguments() const { return run_arguments_; }
	void set_run_arguments(const std::string &args) { run_arguments_ = args; }

	std::string get_run_target_mode() const { return run_target_mode_; }
	void set_run_target_mode(const std::string &mode) { run_target_mode_ = mode; }

	bool get_gdb_auto_continue() const { return gdb_auto_continue_; }
	void set_gdb_auto_continue(bool val) { gdb_auto_continue_ = val; }

      private:
	config_manager() = default;
	std::string get_config_file_path() const;

	std::string clang_format_style_{"file"};
	std::string build_system_{"meson"};
	std::string build_directory_{"build"};
	std::string default_model_id_{"Qwen/Qwen3-Coder-Next-FP8"};
	bool lsp_enabled_{true};
	bool auto_open_error_files_{true};
	bool compile_on_save_{false};
	bool software_map_enabled_{false};
	bool paranoid_mode_{false};
	bool log_all_tool_calls_{false};

	std::string main_executable_{""};
	std::string run_arguments_{""};
	std::string run_target_mode_{"window"};
	bool gdb_auto_continue_{true};
};