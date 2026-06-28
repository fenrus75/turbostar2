#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include "syntax_attribute.h"

class syntax_color_manager
{
      public:
	static syntax_color_manager &get_instance();

	void initialize();

	// Retrieves the ncurses color pair for a syntax attribute
	int get_color_pair(syntax_attribute attr) const;

	// Updates/sets the foreground and background color for a syntax attribute
	void set_color(syntax_attribute attr, uint8_t fg, uint8_t bg);

	// Returns the fg and bg for a syntax attribute
	std::pair<uint8_t, uint8_t> get_color(syntax_attribute attr) const;

	// Human-readable name for a syntax attribute
	static std::string get_attribute_name(syntax_attribute attr);

	void save();
	void load();
	void reload();

      private:
	syntax_color_manager() = default;
	~syntax_color_manager() = default;

	// Map of attribute to ncurses color pair ID
	std::map<syntax_attribute, int> attribute_to_pair_;

	// Map of attribute to original {fg, bg} settings
	std::map<syntax_attribute, std::pair<uint8_t, uint8_t>> attribute_colors_;
};
