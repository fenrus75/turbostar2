#include "fs_multi_replace_content.h"
#include "../../agentlib/tool_registry.h"
#include <filesystem>
#include <format>

namespace tools {

bool fs_multi_replace_content_validator::validate_args_impl(
	const nlohmann::json &untrusted_args,
	const agentlib::tool_context &ctx,
	std::string &out_error) const
{
	try {
		if (!untrusted_args.contains("path") || !untrusted_args["path"].is_string()) {
			out_error = "Missing or invalid 'path' parameter.";
			return false;
		}
		std::string untrusted_path = untrusted_args["path"].get<std::string>();

		std::string check_path = untrusted_path;
		if (check_path.starts_with("file://")) {
			check_path = check_path.substr(7);
		}

		std::string canonical_path;
		if (!ctx.fs_security.validate_access(check_path, agentlib::access_type::write, canonical_path, out_error)) {
			return false;
		}

		args_.path = untrusted_path;
		args_.safe_path = canonical_path;
		args_.chunks.clear();

		if (untrusted_args.contains("strict") && untrusted_args["strict"].is_boolean()) {
			args_.strict = untrusted_args["strict"].get<bool>();
		}

		if (!untrusted_args.contains("chunks") || !untrusted_args["chunks"].is_array() || untrusted_args["chunks"].empty()) {
			out_error = "Missing or empty 'chunks' array parameter.";
			return false;
		}

		for (size_t i = 0; i < untrusted_args["chunks"].size(); ++i) {
			const auto &untrusted_chunk = untrusted_args["chunks"][i];
			if (!untrusted_chunk.is_object()) {
				out_error = std::format("Chunk {} is not a valid JSON object.", i + 1);
				return false;
			}
			if (!untrusted_chunk.contains("target_content") || !untrusted_chunk["target_content"].is_string() ||
			    !untrusted_chunk.contains("replacement_content") || !untrusted_chunk["replacement_content"].is_string()) {
				out_error = std::format("Chunk {} is missing 'target_content' or 'replacement_content' string parameter.", i + 1);
				return false;
			}

			replace_chunk chunk;
			chunk.target_content = untrusted_chunk["target_content"].get<std::string>();
			chunk.replacement_content = untrusted_chunk["replacement_content"].get<std::string>();

			if (untrusted_chunk.contains("line_hint") && untrusted_chunk["line_hint"].is_number_integer()) {
				chunk.line_hint = untrusted_chunk["line_hint"].get<int>();
			}
			if (untrusted_chunk.contains("function_scope") && untrusted_chunk["function_scope"].is_string()) {
				chunk.function_scope = untrusted_chunk["function_scope"].get<std::string>();
			}
			if (untrusted_chunk.contains("start_line") && untrusted_chunk["start_line"].is_number_integer()) {
				chunk.start_line = untrusted_chunk["start_line"].get<int>();
			}
			if (untrusted_chunk.contains("end_line") && untrusted_chunk["end_line"].is_number_integer()) {
				chunk.end_line = untrusted_chunk["end_line"].get<int>();
			}

			args_.chunks.push_back(chunk);
		}

		return true;
	} catch (const std::exception &e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> fs_multi_replace_content_validator::create_tool_impl(const nlohmann::json & /*raw_json*/) const
{
	return std::make_unique<fs_multi_replace_content_tool>(args_);
}

std::vector<agentlib::tool_example> fs_multi_replace_content_validator::get_examples() const
{
	return {
		{
			"Atomic Multi-Chunk Non-Contiguous Scoped Function Edits",
			nlohmann::json{
				{"path", "src/server.cpp"},
				{"chunks", nlohmann::json::array({
					nlohmann::json{
						{"function_scope", "parse_config"},
						{"target_content", "int timeout = 5;"},
						{"replacement_content", "int timeout = 10;"}
					},
					nlohmann::json{
						{"function_scope", "process_request"},
						{"target_content", "bool debug = false;"},
						{"replacement_content", "bool debug = true;"}
					}
				})}
			},
			"Applies multiple non-contiguous line replacements across different function scopes in a single atomic transaction."
		}
	};
}

REGISTER_TOOL(fs_multi_replace_content_validator)


} // namespace tools
