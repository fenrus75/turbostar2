#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace turbostar
{

struct template_info {
	std::string id;				      // e.g. "meson_cpp"
	std::string name;			      // e.g. "Meson + C++"
	std::string language;			      // e.g. "C++"
	std::string buildsystem;		      // e.g. "Meson"
	std::vector<std::string> supported_standards; // e.g. {"C++23", "C++20", "C++17"}
	std::string default_standard;		      // e.g. "C++23"
};

struct project_create_options {
	std::string project_name;		 // e.g. "my_app"
	std::string executable_name;		 // e.g. "my_app"
	std::string language;			 // e.g. "C++"
	std::string buildsystem;		 // e.g. "Meson"
	std::string language_standard;		 // e.g. "C++23"
	std::filesystem::path target_directory; // e.g. "/path/to/my_app"
	bool init_git{true};
};

struct ui_choice_item {
	std::string name;
	char hotkey;
};

struct language_preset_definition {
	std::string language;
	char hotkey;
	std::vector<ui_choice_item> standards;
	std::vector<ui_choice_item> buildsystems;
	std::string default_standard;
	std::string default_buildsystem;
};

class project_template_manager
{
      public:
	static project_template_manager &get_instance();

	// Returns available project templates
	std::vector<template_info> get_available_templates() const;

	// Returns canonical language preset definitions shared between wizard and settings
	static const std::vector<language_preset_definition> &get_language_presets();

	// Finds a preset definition by language name, or nullptr if not found
	static const language_preset_definition *find_language_preset(std::string_view language);

	// Instantiates a new project based on choices, substituting @@VAR@@ tokens
	// and resolving version overrides (e.g. .C++17). Initializes Git if requested.
	bool create_project(const project_create_options &opts, std::string &out_error);

	// Returns true if directory is empty or contains only hidden files/system entries
	static bool is_directory_empty(const std::filesystem::path &dir);

      private:
	project_template_manager() = default;
};

} // namespace turbostar
