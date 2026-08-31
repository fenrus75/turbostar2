#pragma once

#include <nlohmann/json.hpp>
#include <cstdint>
#include <string_view>

namespace json_utils
{

// Parses a numeric value (either integer or string representation like "4224" or "0x1080") from a JSON object safely.
// Performs safety checks and auto-recognizes decimal vs hex ("0x" / "0X") strings.
// Returns def_val if the key is missing, null, not a number/string, or invalid.
uint64_t parse_numeric_from_json(const nlohmann::json &j, std::string_view key, uint64_t def_val = 0) noexcept;

} // namespace json_utils
