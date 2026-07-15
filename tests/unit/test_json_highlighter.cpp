#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include "highlighter/json_highlighter.h"
#include "line.h"

int main()
{
	std::cout << "Running test_json_highlighter..." << std::endl;

	json_highlighter hl;

	// Verify supports_file
	assert(hl.supports_file("config.json") == true);
	assert(hl.supports_file("config.JSON") == false); // case-sensitive filesystem path check
	assert(hl.supports_file("config.txt") == false);

	// Verify supports_language
	assert(hl.supports_language("json") == true);
	assert(hl.supports_language("json5") == false);

	// Test Line 1: Standard key-value pair and punctuation
	auto line1 = std::make_shared<line>("  \"my_key\" : \"my_value\",");
	hl.highlight(line1);

	std::vector<syntax_attribute> attrs1;
	std::string text1;
	line1->get_content(text1, attrs1);

	// "my_key" should be highlighted as keyword
	// It starts at char index 2, length 8.
	assert(attrs1[2] == syntax_attribute::keyword);
	assert(attrs1[9] == syntax_attribute::keyword);

	// Colon ':' at char index 11 should be heading
	assert(attrs1[11] == syntax_attribute::heading);

	// "my_value" should be highlighted as string_literal
	// It starts at char index 13, length 10.
	assert(attrs1[13] == syntax_attribute::string_literal);
	assert(attrs1[22] == syntax_attribute::string_literal);

	// Comma ',' at char index 23 should be heading
	assert(attrs1[23] == syntax_attribute::heading);

	// Test Line 2: Numbers, booleans, null
	auto line2 = std::make_shared<line>("  \"num\": -45.67, \"bool\": true, \"nil\": null");
	hl.highlight(line2);

	std::vector<syntax_attribute> attrs2;
	std::string text2;
	line2->get_content(text2, attrs2);

	// -45.67 should be highlighted as italic
	// It starts at char index 9, length 6.
	assert(attrs2[9] == syntax_attribute::italic);
	assert(attrs2[14] == syntax_attribute::italic);

	// true should be highlighted as keyword
	// It starts at char index 25, length 4.
	assert(attrs2[25] == syntax_attribute::keyword);
	assert(attrs2[28] == syntax_attribute::keyword);

	// null should be highlighted as keyword
	// It starts at char index 38, length 4.
	assert(attrs2[38] == syntax_attribute::keyword);
	assert(attrs2[41] == syntax_attribute::keyword);

	// Test Line 3: Comments
	auto line3 = std::make_shared<line>("  \"port\": 8080 // This is a comment");
	hl.highlight(line3);

	std::vector<syntax_attribute> attrs3;
	std::string text3;
	line3->get_content(text3, attrs3);

	// Comment starts at index 15: // ...
	assert(attrs3[15] == syntax_attribute::comment);
	assert(attrs3[33] == syntax_attribute::comment);

	std::cout << "All test_json_highlighter tests passed!" << std::endl;
	return 0;
}
