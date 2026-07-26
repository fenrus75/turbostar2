#include "project_template_manager.h"
#include "project_templates_embedded.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <format>
#include <fstream>
#include <map>

namespace turbostar
{

project_template_manager &project_template_manager::get_instance()
{
	static project_template_manager instance;
	return instance;
}

std::vector<template_info> project_template_manager::get_available_templates() const
{
	return {
	    {"meson_cpp", "Meson + C++", "C++", "Meson", {"C++23", "C++20", "C++17"}, "C++23"},
	    {"meson_c", "Meson + C", "C", "Meson", {"C17", "C11", "C99"}, "C17"},
	    {"cmake_cpp", "CMake + C++", "C++", "CMake", {"C++23", "C++20", "C++17"}, "C++23"},
	    {"cmake_c", "CMake + C", "C", "CMake", {"C17", "C11", "C99"}, "C17"},
	    {"python_basic", "Python Application", "Python", "pyproject.toml", {"3.11+", "3.10", "3.9"}, "3.11+"},
	    {"cargo_rust", "Rust Cargo Application", "Rust", "Cargo", {"2021 Edition", "2018 Edition"}, "2021 Edition"},
	};
}

bool project_template_manager::is_directory_empty(const std::filesystem::path &dir)
{
	if (!std::filesystem::exists(dir)) {
		return true;
	}

	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
		std::string name = entry.path().filename().string();
		if (name != "." && name != ".." && !name.starts_with(".git")) {
			return false;
		}
	}

	return true;
}

static std::string get_cmd_output(const std::string &cmd)
{
	std::array<char, 256> buffer;
	std::string result;
	FILE *pipe = popen(cmd.c_str(), "r");
	if (!pipe) {
		return "";
	}
	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
		result += buffer.data();
	}
	pclose(pipe);
	while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
		result.pop_back();
	}
	return result;
}

static std::string replace_all(std::string str, const std::string &from, const std::string &to)
{
	if (from.empty()) return str;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length();
	}
	return str;
}

bool project_template_manager::create_project(const project_create_options &opts, std::string &out_error)
{
	out_error.clear();

	std::string target_template_id;
	auto templates = get_available_templates();
	for (const auto &tmpl : templates) {
		if (tmpl.language == opts.language && tmpl.buildsystem == opts.buildsystem) {
			target_template_id = tmpl.id;
			break;
		}
	}

	if (target_template_id.empty()) {
		out_error = std::format("No matching template found for language '{}' and build system '{}'.", opts.language, opts.buildsystem);
		return false;
	}

	std::filesystem::create_directories(opts.target_directory);

	std::string author_name = get_cmd_output("git config user.name");
	if (author_name.empty()) author_name = "Developer";

	std::string author_email = get_cmd_output("git config user.email");
	if (author_email.empty()) author_email = "dev@example.com";

	auto now = std::chrono::system_clock::now();
	std::time_t now_c = std::chrono::system_clock::to_time_t(now);
	std::tm tm_buf{};
	localtime_r(&now_c, &tm_buf);
	std::string current_year = std::to_string(tm_buf.tm_year + 1900);

	std::string project_name_lower = opts.project_name;
	std::transform(project_name_lower.begin(), project_name_lower.end(), project_name_lower.begin(), [](unsigned char c) { return std::tolower(c); });

	// Group embedded files for target_template_id
	std::string prefix = target_template_id + "/";
	std::map<std::string, std::string> selected_files; // dest_rel_path -> content

	for (size_t i = 0; i < EMBEDDED_TEMPLATES_COUNT; ++i) {
		std::string_view rel_path = EMBEDDED_TEMPLATES[i].relative_path;
		if (rel_path.starts_with(prefix)) {
			std::string sub_path(rel_path.substr(prefix.length()));
			std::string content(EMBEDDED_TEMPLATES[i].content, EMBEDDED_TEMPLATES[i].size);

			// Check for version override suffix (e.g. .C++17)
			std::string version_suffix = "." + opts.language_standard;
			if (sub_path.ends_with(version_suffix)) {
				std::string base_path = sub_path.substr(0, sub_path.length() - version_suffix.length());
				selected_files[base_path] = content;
			} else if (sub_path.find('.') != std::string::npos &&
			           (sub_path.ends_with(".C++23") || sub_path.ends_with(".C++20") || sub_path.ends_with(".C++17") ||
			            sub_path.ends_with(".C17") || sub_path.ends_with(".C11") || sub_path.ends_with(".C99"))) {
				// Ignore other version suffix overrides
				continue;
			} else {
				// Base file: store if not already overridden by a specific version suffix
				if (selected_files.find(sub_path) == selected_files.end()) {
					selected_files[sub_path] = content;
				}
			}
		}
	}

	// Instantiate each selected file
	for (const auto &[rel_path, raw_content] : selected_files) {
		std::string substituted = raw_content;
		substituted = replace_all(substituted, "@@PROJECT_NAME@@", opts.project_name);
		substituted = replace_all(substituted, "@@PROJECT_NAME_LOWER@@", project_name_lower);
		substituted = replace_all(substituted, "@@EXECUTABLE_NAME@@", opts.executable_name);
		substituted = replace_all(substituted, "@@AUTHOR_NAME@@", author_name);
		substituted = replace_all(substituted, "@@AUTHOR_EMAIL@@", author_email);
		substituted = replace_all(substituted, "@@YEAR@@", current_year);
		substituted = replace_all(substituted, "@@LANGUAGE_STD@@", opts.language_standard);

		auto dest_file_path = opts.target_directory / rel_path;
		std::filesystem::create_directories(dest_file_path.parent_path());

		std::ofstream ofs(dest_file_path, std::ios::binary);
		if (!ofs) {
			out_error = std::format("Failed to write template file: {}", dest_file_path.string());
			return false;
		}
		ofs << substituted;
	}

	// Initialize Git repository if requested
	if (opts.init_git) {
		std::string git_init_cmd = std::format("git -C \"{}\" init -b main 2>&1", opts.target_directory.string());
		(void)get_cmd_output(git_init_cmd);

		std::string git_add_cmd = std::format("git -C \"{}\" add . 2>&1", opts.target_directory.string());
		(void)get_cmd_output(git_add_cmd);

		std::string git_commit_cmd = std::format("git -C \"{}\" commit -m \"Initial commit from Turbostar template\" 2>&1", opts.target_directory.string());
		(void)get_cmd_output(git_commit_cmd);
	}

	return true;
}

} // namespace turbostar
