// Tested source file: src/agentlib/json_utils.cpp
#include "agentlib/json_utils.h"
#include "test_watchdog.h"
#include <cassert>
#include <iostream>

int main()
{
	test_watchdog::setup_watchdog();

	nlohmann::json j = {
		{"int_val", 4224},
		{"hex_str", "0x1080"},
		{"HEX_STR", "0X1080"},
		{"dec_str", "4224"},
		{"invalid_str", "not_a_number"},
		{"null_val", nullptr},
		{"neg_val", -10}
	};

	assert(json_utils::parse_numeric_from_json(j, "int_val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(j, "hex_str", 0) == 0x1080);
	assert(json_utils::parse_numeric_from_json(j, "HEX_STR", 0) == 0x1080);
	assert(json_utils::parse_numeric_from_json(j, "dec_str", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(j, "invalid_str", 99) == 99);
	assert(json_utils::parse_numeric_from_json(j, "null_val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(j, "missing_key", 99) == 99);
	assert(json_utils::parse_numeric_from_json(j, "neg_val", 99) == 99);

	// Test json_utils::get_number flexible parsing
	int val = 0;
	std::string err;
	assert(json_utils::get_number(j, "int_val", val, -1, err) && val == 4224);
	assert(json_utils::get_number(j, "dec_str", val, -1, err) && val == 4224);
	assert(json_utils::get_number(j, "hex_str", val, -1, err) && val == 0x1080);
	assert(json_utils::get_number(j, "HEX_STR", val, -1, err) && val == 0x1080);

	// Test missing key defaults
	assert(json_utils::get_number(j, "missing_key", val, 100, err) && val == 100);

	// Test garbage rejection
	assert(!json_utils::get_number(j, "invalid_str", val, -1, err) && !err.empty());

	// Test partial numeric garbage rejection ("12abc")
	nlohmann::json j_garbage = {{"partial_str", "12abc"}};
	assert(!json_utils::get_number(j_garbage, "partial_str", val, -1, err) && !err.empty());

	std::cout << "test_json_utils passed!" << std::endl;
	return 0;
}
