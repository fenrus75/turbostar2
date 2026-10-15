// Tested source file: src/agentlib/json_utils.cpp
#include "agentlib/json_utils.h"
#include <charconv>
#include <string>

namespace json_utils
{

// Parses a numeric value from a JSON object safely, returning def_val on missing keys, null,
// or invalid formats.
// Supported types:
// 1. Native unsigned integers (uint64_t)
// 2. Native non-negative signed integers (negative integers fall back to def_val)
// 3. Native non-negative floating point numbers (truncated to uint64_t; negative floats fall back to def_val)
// 4. Strings: trims leading/trailing whitespace, auto-detects decimal vs hex ("0x" / "0X"),
//    and validates that the entire string is consumed with no trailing or embedded garbage.
// Rejects: negative strings, floating point strings, empty strings, partial numbers ("12abc"),
// spaces in between digits, and invalid hex ("0x", "0xG").
uint64_t parse_numeric_from_json(const nlohmann::json &j, std::string_view key, uint64_t def_val) noexcept
{
	try {
		if (!j.is_object()) {
			return def_val;
		}

		auto it = j.find(key);
		if (it == j.end() || it->is_null()) {
			return def_val;
		}

		const auto &v = *it;
		if (v.is_number_unsigned()) {
			return v.get<uint64_t>();
		}
		if (v.is_number_integer()) {
			int64_t val = v.get<int64_t>();
			return val < 0 ? def_val : static_cast<uint64_t>(val);
		}
		if (v.is_number_float()) {
			double val = v.get<double>();
			return val < 0.0 ? def_val : static_cast<uint64_t>(val);
		}
		if (v.is_string()) {
			std::string s = v.get<std::string>();
			size_t first = s.find_first_not_of(" \t\n\r");
			if (first == std::string::npos) {
				return def_val;
			}
			size_t last = s.find_last_not_of(" \t\n\r");
			s = s.substr(first, (last - first + 1));

			// Negative strings are not valid unsigned 64-bit values
			if (s.starts_with('-')) {
				return def_val;
			}

			int base = 10;
			if (s.starts_with("0x") || s.starts_with("0X")) {
				base = 16;
			}

			size_t processed = 0;
			uint64_t res = std::stoull(s, &processed, base);
			if (processed != s.length()) {
				return def_val;
			}
			return res;
		}
	} catch (...) {
		return def_val;
	}
	return def_val;
}

} // namespace json_utils
