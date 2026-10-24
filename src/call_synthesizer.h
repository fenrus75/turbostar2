#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "document.h"

namespace turbostar
{

struct variable_info {
	std::string name;
	std::string type;
	int line{0};
};

struct signature_param {
	std::string type;
	std::string name;
	std::string default_value;
};

struct signature_info {
	std::string function_name;
	std::string return_type;
	std::vector<signature_param> parameters;
	std::string raw_signature;
};

struct call_suggestion {
	std::string call_text;     // e.g. "(x_, y_, width_, height_, 1, pair)"
	std::string display_label; // Label for menu display
	int score{0};              // Confidence score
	bool is_ai{false};
};

enum class symbol_kind_context {
	function_call,
	type_definition,
	variable,
	unknown
};

enum class call_site_case {
	none,
	naked_identifier, // Case 1: func
	half_call,        // Case 2: func( or func(a,
	full_call         // Case 3: func(a, b)
};

struct context_menu_data {
	symbol_kind_context kind{symbol_kind_context::unknown};
	call_site_case call_case{call_site_case::none};
	std::string token;
	std::string type_synopsis;
	std::string declaration_location;
	std::string existing_args;
	std::vector<call_suggestion> suggestions;
	int token_start_col{-1};
	int token_end_col{-1};
	int existing_args_start_col{-1}; // Where '(' begins (-1 if none)
	int existing_args_end_col{-1};   // Where ')' ends (-1 if none)
	int target_line{-1};
	bool has_existing_parens{false};
};

/**
 * @brief Analyzes code context around cursor to synthesize intelligent argument calls and code actions.
 */
class call_synthesizer
{
      public:
	/**
	 * @brief Extracts local variables, member variables, and parameters in scope at target_line.
	 */
	static std::vector<variable_info> extract_in_scope_variables(const document &doc, int target_line);

	/**
	 * @brief Parses a C++ function signature string into structured parameter objects.
	 */
	static std::optional<signature_info> parse_function_signature(std::string_view raw_signature);

	/**
	 * @brief Scores how well a candidate variable matches a parameter based on name and type heuristics.
	 */
	static int score_variable_match(std::string_view param_name, std::string_view param_type, const variable_info &var);

	/**
	 * @brief Generates ranked argument call suggestions using local variables and signature parameters.
	 */
	static std::vector<call_suggestion> generate_argument_suggestions(
	    const signature_info &sig,
	    const std::vector<variable_info> &locals,
	    std::string_view existing_args = "");

	/**
	 * @brief Analyzes the symbol and call site at a document line and column.
	 */
	static context_menu_data analyze_context_at(document &doc, int doc_y, int doc_x);

	/**
	 * @brief Returns true if an AI model is configured in the registry.
	 */
	static bool has_ai_model_configured();

	/**
	 * @brief Queries an LLM to synthesize arguments for the given call context.
	 */
	static std::optional<std::string> query_ai_call_suggestion(
	    std::string_view enclosing_function_code,
	    std::string_view raw_signature,
	    std::span<const variable_info> in_scope_vars,
	    std::string_view existing_args = "");
};

} // namespace turbostar
