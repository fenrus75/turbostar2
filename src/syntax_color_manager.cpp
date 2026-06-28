#include "syntax_color_manager.h"
#include <ncurses.h>
#include <fstream>
#include <nlohmann/json.hpp>

syntax_color_manager &syntax_color_manager::get_instance()
{
	static syntax_color_manager instance;
	return instance;
}

void syntax_color_manager::initialize()
{
	// Check if already initialized (can be called multiple times safely)
	if (!attribute_to_pair_.empty()) {
		return;
	}

	// Register initial defaults
	attribute_colors_[syntax_attribute::normal] = {COLOR_YELLOW + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::keyword] = {COLOR_WHITE + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::comment] = {COLOR_BLACK + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::string_literal] = {COLOR_GREEN + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::heading] = {COLOR_CYAN + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::bold] = {COLOR_YELLOW + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::italic] = {COLOR_CYAN, COLOR_BLUE};
	attribute_colors_[syntax_attribute::list_item] = {COLOR_GREEN + 8, COLOR_BLUE};
	attribute_colors_[syntax_attribute::trailing_space] = {COLOR_WHITE + 8, COLOR_RED};

	// Set static pair IDs
	attribute_to_pair_[syntax_attribute::normal] = 3;
	attribute_to_pair_[syntax_attribute::keyword] = 12;
	attribute_to_pair_[syntax_attribute::comment] = 80;
	attribute_to_pair_[syntax_attribute::string_literal] = 81;
	attribute_to_pair_[syntax_attribute::heading] = 22;
	attribute_to_pair_[syntax_attribute::bold] = 23;
	attribute_to_pair_[syntax_attribute::italic] = 82;
	attribute_to_pair_[syntax_attribute::list_item] = 24;
	attribute_to_pair_[syntax_attribute::trailing_space] = 27;

	// Load custom colors from JSON configuration if available
	load();

	// Initialize the ncurses color pairs
	if (has_colors()) {
		for (const auto &[attr, pair_id] : attribute_to_pair_) {
			auto [fg, bg] = attribute_colors_[attr];
			init_pair(pair_id, fg, bg);
		}
	}
}

int syntax_color_manager::get_color_pair(syntax_attribute attr) const
{
	auto it = attribute_to_pair_.find(attr);
	if (it != attribute_to_pair_.end()) {
		return it->second;
	}
	return 3; // Fallback to normal text pair
}

void syntax_color_manager::set_color(syntax_attribute attr, uint8_t fg, uint8_t bg)
{
	attribute_colors_[attr] = {fg, bg};
	auto it = attribute_to_pair_.find(attr);
	if (it != attribute_to_pair_.end() && has_colors()) {
		init_pair(it->second, fg, bg);
	}
}

std::pair<uint8_t, uint8_t> syntax_color_manager::get_color(syntax_attribute attr) const
{
	auto it = attribute_colors_.find(attr);
	if (it != attribute_colors_.end()) {
		return it->second;
	}
	return {COLOR_YELLOW + 8, COLOR_BLUE};
}

std::string syntax_color_manager::get_attribute_name(syntax_attribute attr)
{
	switch (attr) {
		case syntax_attribute::normal: return "Normal Text";
		case syntax_attribute::keyword: return "Keyword";
		case syntax_attribute::comment: return "Comment";
		case syntax_attribute::string_literal: return "String Literal";
		case syntax_attribute::heading: return "Markdown Heading";
		case syntax_attribute::bold: return "Markdown Bold";
		case syntax_attribute::italic: return "Markdown Italic";
		case syntax_attribute::list_item: return "Markdown List Item";
		case syntax_attribute::trailing_space: return "Trailing Space";
		default: return "Unknown";
	}
}

#include <filesystem>

void syntax_color_manager::load()
{
	const char *home = getenv("HOME");
	std::string path = home ? std::string(home) + "/.cache/turbostar/colors.json" : ".colors.json";
	std::ifstream in(path);
	if (!in.is_open()) {
		return;
	}

	nlohmann::json json_data;
	try {
		in >> json_data;
		for (auto &[key_str, val] : json_data.items()) {
			int attr_val = std::stoi(key_str);
			syntax_attribute attr = static_cast<syntax_attribute>(attr_val);
			uint8_t fg = val["fg"];
			uint8_t bg = val["bg"];
			attribute_colors_[attr] = {fg, bg};
		}
	} catch (...) {
		// Suppress any json parsing/formatting errors silently
	}
}

void syntax_color_manager::save()
{
	const char *home = getenv("HOME");
	std::string path = home ? std::string(home) + "/.cache/turbostar/colors.json" : ".colors.json";
	
	try {
		std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	} catch (...) {
		// Suppress filesystem errors
	}

	std::ofstream out(path);
	if (!out.is_open()) {
		return;
	}

	nlohmann::json json_data;
	for (const auto &[attr, colors] : attribute_colors_) {
		int attr_val = static_cast<int>(attr);
		json_data[std::to_string(attr_val)] = {{"fg", colors.first}, {"bg", colors.second}};
	}
	out << json_data.dump(2);
}

void syntax_color_manager::reload()
{
	load();
	if (has_colors()) {
		for (const auto &[attr, pair_id] : attribute_to_pair_) {
			auto [fg, bg] = attribute_colors_[attr];
			init_pair(pair_id, fg, bg);
		}
	}
}
