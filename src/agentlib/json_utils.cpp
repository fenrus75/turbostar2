// Tested source file: src/agentlib/json_utils.cpp
#include "agentlib/json_utils.h"
#include <charconv>
#include <string>

namespace json_utils
{

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

			if (s.starts_with("0x") || s.starts_with("0X")) {
				return std::stoull(s.substr(2), nullptr, 16);
			}
			return std::stoull(s, nullptr, 10);
		}
	} catch (...) {
		return def_val;
	}
	return def_val;
}

} // namespace json_utils
