#pragma once

#include <optional>
#include <string_view>

namespace languages
{

/**
 * @brief Compiled-in per-language LLM/environment defaults.
 *
 * Provides language-specific defaults such as suggested sampling temperature
 * for LLM API requests based on the active project language setting.
 */
class language_settings
{
      public:
	// Returns the suggested sampling temperature for a given project language
	// (e.g. 0.1 for SystemVerilog/Verilog), or std::nullopt to leave the
	// provider/model default untouched in the API request.
	static std::optional<double> suggested_temperature(std::string_view language) noexcept;

	// Returns the suggested sampling temperature for the current project's
	// configured primary language (from config_manager).
	static std::optional<double> suggested_temperature_for_current_project() noexcept;

	// Returns false for OpenAI reasoning models (o1/o3/o4 series) that reject
	// explicit 'temperature' parameters with HTTP 400.
	static bool model_supports_temperature(std::string_view model_id) noexcept;
};

} // namespace languages
