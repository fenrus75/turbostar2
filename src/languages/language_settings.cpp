#include "languages/language_settings.h"

#include <cctype>
#include <string>

#include "config_manager.h"

namespace languages
{

namespace
{

std::string to_lower_ascii(std::string_view sv)
{
	std::string out;
	out.reserve(sv.size());
	for (unsigned char c : sv) {
		out.push_back(static_cast<char>(std::tolower(c)));
	}
	return out;
}

bool has_reasoning_prefix(std::string_view lower_name) noexcept
{
	// Match OpenAI o1 / o3 / o4 reasoning model families (exact or followed by '-' / '.' / '_')
	auto match_prefix = [&](std::string_view pfx) -> bool {
		if (!lower_name.starts_with(pfx)) {
			return false;
		}
		if (lower_name.size() == pfx.size()) {
			return true;
		}
		char next = lower_name[pfx.size()];
		return next == '-' || next == '.' || next == '_';
	};
	return match_prefix("o1") || match_prefix("o3") || match_prefix("o4");
}

} // namespace

std::optional<double> language_settings::suggested_temperature(std::string_view language) noexcept
{
	std::string lower = to_lower_ascii(language);
	if (lower == "systemverilog" || lower == "verilog") {
		return 0.1;
	}
	// Default: omit temperature from API requests so the model/provider default applies.
	return std::nullopt;
}

std::optional<double> language_settings::suggested_temperature_for_current_project() noexcept
{
	return suggested_temperature(config_manager::get_instance().get_primary_language());
}

bool language_settings::model_supports_temperature(std::string_view model_id) noexcept
{
	if (model_id.empty()) {
		return true;
	}
	// Strip optional provider prefix (e.g. "openai/o3-mini" -> "o3-mini")
	size_t slash = model_id.rfind('/');
	std::string_view base = (slash == std::string_view::npos) ? model_id : model_id.substr(slash + 1);
	std::string lower = to_lower_ascii(base);
	return !has_reasoning_prefix(lower);
}

} // namespace languages
