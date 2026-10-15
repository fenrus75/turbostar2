// Tested source file: src/agentlib/json_utils.cpp
#include "agentlib/json_utils.h"
#include "test_watchdog.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

void test_parse_numeric_native_types()
{
	// Unsigned integer
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 4224u}}, "val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 0u}}, "val", 99) == 0);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 18446744073709551615ULL}}, "val", 0) == 18446744073709551615ULL);

	// Signed integer
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 4224}}, "val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 0}}, "val", 99) == 0);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", -1}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", -4224}}, "val", 99) == 99);

	// Floating point numbers
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 4224.0}}, "val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", 4224.75}}, "val", 0) == 4224); // truncated to integer
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", -10.5}}, "val", 99) == 99);

	// Null, missing, or non-numeric JSON types
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", nullptr}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"other", 10}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json::object(), "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json(42), "val", 99) == 99); // non-object json
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", true}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", false}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", nlohmann::json::array({1, 2})}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", nlohmann::json::object({{"a", 1}})}}, "val", 99) == 99);
}

void test_parse_numeric_strings()
{
	// Decimal strings
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0"}}, "val", 99) == 0);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "4224"}}, "val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "+4224"}}, "val", 0) == 4224);

	// Hex strings
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x0"}}, "val", 99) == 0);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0X0"}}, "val", 99) == 0);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x1080"}}, "val", 0) == 0x1080);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0X1080"}}, "val", 0) == 0x1080);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0xabcdef"}}, "val", 0) == 0xabcdef);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0XABCDEF"}}, "val", 0) == 0xabcdef);

	// Whitespace-padded strings
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "  4224  "}}, "val", 0) == 4224);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "\t\n0x1080\r\n"}}, "val", 0) == 0x1080);

	// Empty strings
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", ""}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "   "}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "\t\r\n"}}, "val", 99) == 99);
}

void test_parse_numeric_mixes_and_garbage()
{
	// Prefix numbers with trailing characters
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12abc"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "42px"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "100%"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x1080xyz"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x10G"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "123_456"}}, "val", 99) == 99);

	// Prefix characters with trailing numbers
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "abc12"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "x42"}}, "val", 99) == 99);

	// Punctuation and brackets
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "[123]"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "(42)"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "10-20"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "1/2"}}, "val", 99) == 99);

	// Double and mixed signs
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "--10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "++10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "+-10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "-+10"}}, "val", 99) == 99);

	// Negative string numbers must return def_val for uint64_t
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "-10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "-0x10"}}, "val", 99) == 99);
}

void test_parse_numeric_spaces_in_weird_places()
{
	// Spaces between digits
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "1 2"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12 34"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "1  2  3"}}, "val", 99) == 99);

	// Spaces after signs
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "+ 10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "- 10"}}, "val", 99) == 99);

	// Spaces inside hex prefix or between prefix and digits
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0 x10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0X 10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x 10"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", " 0x 10 "}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0x"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "0X"}}, "val", 99) == 99);
}

void test_parse_numeric_utf8()
{
	// Emojis mixed with numbers
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "123🦀"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "🦀123"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "123🚀456"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "🦀"}}, "val", 99) == 99);

	// Fullwidth and non-ASCII digits (must not be treated as ASCII digits)
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "１２３"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "१२३"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "١٢٣"}}, "val", 99) == 99);

	// Unicode whitespace: non-breaking space (U+00A0)
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12\u00A034"}}, "val", 99) == 99);

	// Multibyte characters mixed with digits
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "café123"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "123café"}}, "val", 99) == 99);
}

void test_parse_numeric_dot_vs_comma()
{
	// Floating point strings with dot or comma must be rejected by integer parse_numeric_from_json
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12.34"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12,34"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", ".123"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", ",123"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12."}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "12,"}}, "val", 99) == 99);
}

void test_parse_numeric_overflow()
{
	// Max uint64 string
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "18446744073709551615"}}, "val", 0) == 18446744073709551615ULL);

	// Overflow uint64 string (18446744073709551616 is 2^64)
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "18446744073709551616"}}, "val", 99) == 99);
	assert(json_utils::parse_numeric_from_json(nlohmann::json{{"val", "999999999999999999999999999999"}}, "val", 99) == 99);
}

void test_get_number_types()
{
	std::string err;

	// 1. Signed int
	int int_val = 0;
	assert(json_utils::get_number(nlohmann::json{{"k", 42}}, "k", int_val, -1, err) && int_val == 42);
	assert(json_utils::get_number(nlohmann::json{{"k", -42}}, "k", int_val, -1, err) && int_val == -42);
	assert(json_utils::get_number(nlohmann::json{{"k", "42"}}, "k", int_val, -1, err) && int_val == 42);
	assert(json_utils::get_number(nlohmann::json{{"k", "-42"}}, "k", int_val, -1, err) && int_val == -42);
	assert(json_utils::get_number(nlohmann::json{{"k", "0x2A"}}, "k", int_val, -1, err) && int_val == 42);
	assert(json_utils::get_number(nlohmann::json{{"k", "-0x2A"}}, "k", int_val, -1, err) && int_val == -42);
	assert(!json_utils::get_number(nlohmann::json{{"k", "42abc"}}, "k", int_val, -1, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "42.5"}}, "k", int_val, -1, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "42,5"}}, "k", int_val, -1, err) && !err.empty());

	// 2. Unsigned int (uint64_t and size_t) - must reject negative values
	uint64_t uint_val = 0;
	uint64_t uint_def = 0;
	assert(json_utils::get_number(nlohmann::json{{"k", 42u}}, "k", uint_val, uint_def, err) && uint_val == 42);
	assert(json_utils::get_number(nlohmann::json{{"k", "42"}}, "k", uint_val, uint_def, err) && uint_val == 42);
	assert(json_utils::get_number(nlohmann::json{{"k", "0x2A"}}, "k", uint_val, uint_def, err) && uint_val == 42);
	assert(!json_utils::get_number(nlohmann::json{{"k", -10}}, "k", uint_val, uint_def, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", -10.5}}, "k", uint_val, uint_def, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "-10"}}, "k", uint_val, uint_def, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "-0x10"}}, "k", uint_val, uint_def, err) && !err.empty());

	size_t sz_val = 0;
	assert(json_utils::get_number(nlohmann::json{{"k", "100"}}, "k", sz_val, static_cast<size_t>(0), err) && sz_val == 100);
	assert(!json_utils::get_number(nlohmann::json{{"k", "-100"}}, "k", sz_val, static_cast<size_t>(0), err) && !err.empty());

	// 3. Floating point (double)
	double dbl_val = 0.0;
	assert(json_utils::get_number(nlohmann::json{{"k", 12.34}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - 12.34) < 1e-6);
	assert(json_utils::get_number(nlohmann::json{{"k", "12.34"}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - 12.34) < 1e-6);
	assert(json_utils::get_number(nlohmann::json{{"k", ".5"}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - 0.5) < 1e-6);
	assert(json_utils::get_number(nlohmann::json{{"k", "12."}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - 12.0) < 1e-6);
	assert(json_utils::get_number(nlohmann::json{{"k", "-12.34"}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - (-12.34)) < 1e-6);
	assert(json_utils::get_number(nlohmann::json{{"k", "  12.34  "}}, "k", dbl_val, 0.0, err) && std::abs(dbl_val - 12.34) < 1e-6);

	// Comma should be rejected (C locale uses '.' for decimal mark)
	assert(!json_utils::get_number(nlohmann::json{{"k", "12,34"}}, "k", dbl_val, 0.0, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", ",5"}}, "k", dbl_val, 0.0, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "12,"}}, "k", dbl_val, 0.0, err) && !err.empty());

	// Multiple decimal points or garbage
	assert(!json_utils::get_number(nlohmann::json{{"k", "12.34.56"}}, "k", dbl_val, 0.0, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "12.34abc"}}, "k", dbl_val, 0.0, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "12.34🦀"}}, "k", dbl_val, 0.0, err) && !err.empty());
	assert(!json_utils::get_number(nlohmann::json{{"k", "- 12.34"}}, "k", dbl_val, 0.0, err) && !err.empty());

	// Defaults for missing and null keys
	assert(json_utils::get_number(nlohmann::json::object(), "missing", int_val, 999, err) && int_val == 999);
	assert(json_utils::get_number(nlohmann::json{{"k", nullptr}}, "k", int_val, 999, err) && int_val == 999);
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_parse_numeric_native_types();
	test_parse_numeric_strings();
	test_parse_numeric_mixes_and_garbage();
	test_parse_numeric_spaces_in_weird_places();
	test_parse_numeric_utf8();
	test_parse_numeric_dot_vs_comma();
	test_parse_numeric_overflow();
	test_get_number_types();

	std::cout << "All json_utils unit tests passed successfully!" << std::endl;
	return 0;
}
