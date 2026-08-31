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

	std::cout << "test_json_utils passed!" << std::endl;
	return 0;
}
