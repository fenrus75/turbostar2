#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace json_utils
{

// Parses a numeric value (either integer or string representation like "4224" or "0x1080") from a JSON object safely.
// Performs safety checks and auto-recognizes decimal vs hex ("0x" / "0X") strings.
// Returns def_val if the key is missing, null, not a number/string, or invalid.
uint64_t parse_numeric_from_json(const nlohmann::json &j, std::string_view key, uint64_t def_val = 0) noexcept;

/**
 * @brief Safely parses a numeric parameter from a JSON object with support for integers, floats,
 *        and numeric strings (decimal or hex 0x.../0X...).
 * @tparam T Integer or floating point type (int, int64_t, uint64_t, double, size_t).
 * @param j The JSON object.
 * @param key The argument key to look up.
 * @param out_val Output variable where parsed numeric value will be written.
 * @param default_val Value to assign if the key is missing or null.
 * @param out_error Output error message if parsing fails due to invalid/garbage input.
 * @return true if successfully parsed or default assigned; false if argument was invalid garbage.
 */
template <typename T>
bool get_number(const nlohmann::json &j, std::string_view key, T &out_val, T default_val, std::string &out_error) noexcept
{
	try {
		if (!j.is_object()) {
			out_val = default_val;
			return true;
		}

		auto it = j.find(key);
		if (it == j.end() || it->is_null()) {
			out_val = default_val;
			return true;
		}

		const auto &v = *it;

		// 1. Native unsigned integer
		if (v.is_number_unsigned()) {
			out_val = static_cast<T>(v.get<uint64_t>());
			return true;
		}
		// 2. Native signed integer
		if (v.is_number_integer()) {
			out_val = static_cast<T>(v.get<int64_t>());
			return true;
		}
		// 3. Native float/double
		if (v.is_number_float()) {
			out_val = static_cast<T>(v.get<double>());
			return true;
		}
		// 4. String coercion (decimal, hex 0x/0X, whitespace-padded)
		if (v.is_string()) {
			std::string s = v.get<std::string>();
			size_t first = s.find_first_not_of(" \t\n\r");
			if (first == std::string::npos) {
				out_error = std::format("Parameter '{}' cannot be an empty numeric string.", key);
				return false;
			}
			size_t last = s.find_last_not_of(" \t\n\r");
			s = s.substr(first, (last - first + 1));

			size_t processed_chars = 0;
			if constexpr (std::is_floating_point_v<T>) {
				double parsed_dbl = std::stod(s, &processed_chars);
				if (processed_chars != s.length()) {
					out_error = std::format("Parameter '{}' contains trailing invalid characters: '{}'.", key, s);
					return false;
				}
				out_val = static_cast<T>(parsed_dbl);
				return true;
			} else if constexpr (std::is_signed_v<T>) {
				int base = 10;
				if (s.starts_with("0x") || s.starts_with("0X")) {
					base = 16;
				} else if (s.starts_with("-0x") || s.starts_with("-0X")) {
					base = 16;
					s.erase(1, 2); // remove "0x" after '-'
				}
				int64_t parsed_int = std::stoll(s, &processed_chars, base);
				if (processed_chars != s.length()) {
					out_error = std::format("Parameter '{}' contains trailing invalid characters: '{}'.", key, s);
					return false;
				}
				out_val = static_cast<T>(parsed_int);
				return true;
			} else {
				int base = 10;
				if (s.starts_with("0x") || s.starts_with("0X")) {
					base = 16;
				}
				uint64_t parsed_uint = std::stoull(s, &processed_chars, base);
				if (processed_chars != s.length()) {
					out_error = std::format("Parameter '{}' contains trailing invalid characters: '{}'.", key, s);
					return false;
				}
				out_val = static_cast<T>(parsed_uint);
				return true;
			}
		}

		out_error = std::format("Parameter '{}' must be a number or numeric string.", key);
		return false;
	} catch (const std::exception &e) {
		out_error = std::format("Invalid numeric parameter '{}': {}.", key, e.what());
		return false;
	} catch (...) {
		out_error = std::format("Invalid numeric parameter '{}'.", key);
		return false;
	}
}

} // namespace json_utils
