#include "apply_text_filter.h"
#include "../../filter_registry.h"
#include <fstream>
#include <sstream>

namespace tools
{

apply_text_filter_tool::apply_text_filter_tool(apply_text_filter_args args)
    : args_(std::move(args))
{
}

bool apply_text_filter_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	// Path validation was already done at Stage 1, and no live editor state
	// checks are needed for pure text filtering.
	return true;
}

std::string apply_text_filter_tool::execute(agentlib::tool_context & /*ctx*/)
{
	std::string input_text = args_.text;
	if (input_text.empty() && !args_.safe_path.empty()) {
		std::ifstream ifs(args_.safe_path, std::ios::binary);
		if (!ifs.is_open()) {
			return "Error: Failed to open input file '" + args_.path + "' for reading.";
		}
		input_text = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
		ifs.close();
	}

	bool success = false;
	std::string result = agentlib::filter_registry::get_instance().apply_filter(args_.filter, input_text, success);
	if (!success) {
		return "Error: Failed to apply filter '" + args_.filter + "'.";
	}

	if (!args_.safe_output_path.empty()) {
		std::ofstream out(args_.safe_output_path, std::ios::binary);
		if (!out.is_open()) {
			return "Error: Failed to open output path '" + args_.output_path + "' for writing.";
		}
		out << result;
		if (out.fail()) {
			return "Error: Failed to write filtered content to '" + args_.output_path + "'.";
		}
		return "Successfully applied filter '" + args_.filter + "' and wrote output to '" + args_.output_path + "'.";
	}

	return result;
}

} // namespace tools
