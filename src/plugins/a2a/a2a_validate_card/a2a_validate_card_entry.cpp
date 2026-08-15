#include "a2a_validate_card.h"
#include "fs_utils.h"
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

namespace tools
{

a2a_validate_card_tool::a2a_validate_card_tool(a2a_validate_card_args args)
	: llm_tool_action("Validating A2A Agent Card")
	, args_(std::move(args))
{
}

bool a2a_validate_card_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

static std::string sanitize_card_str(const std::string &untrusted_str)
{
	std::string clean;
	clean.reserve(untrusted_str.size());
	for (unsigned char c : untrusted_str) {
		if (c < 32 || c == 127) continue;
		clean.push_back(static_cast<char>(c));
	}
	return clean;
}

std::string a2a_validate_card_tool::execute(agentlib::tool_context &ctx)
{
	std::string json_str = args_.card_data;

	if (!args_.safe_path.empty()) {
		if (args_.safe_path.find("://") != std::string::npos) {
			auto vfs = ctx.fs_security.get_vfs();
			if (vfs) {
				auto view_opt = vfs->read_file(args_.safe_path);
				if (view_opt) {
					std::string_view v = view_opt.value()->view();
					if (v.size() > 1048576) {
						set_failure(ctx, "File exceeds maximum size limit of 1MB");
						return "Error: Card file exceeds maximum size limit of 1MB.";
					}
					json_str = std::string(v);
				} else {
					set_failure(ctx, "Virtual file not found");
					return "Error: Virtual file not found or empty: " + args_.requested_path;
				}
			} else {
				set_failure(ctx, "VFS not available");
				return "Error: VFS not available to read virtual path.";
			}
		} else {
			if (!fs_utils::is_regular_file(args_.safe_path)) {
				set_failure(ctx, "Not a regular file");
				return "Error: Target is not a regular file: " + args_.requested_path;
			}
			std::ifstream f(args_.safe_path, std::ios::binary | std::ios::ate);
			if (!f.is_open()) {
				set_failure(ctx, "Could not open file");
				return "Error: Could not open file for reading: " + args_.requested_path;
			}
			auto sz = f.tellg();
			if (sz > 1048576) {
				set_failure(ctx, "File exceeds maximum size limit of 1MB");
				return "Error: Card file exceeds maximum size limit of 1MB.";
			}
			f.seekg(0, std::ios::beg);
			json_str.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		}
	}

	if (json_str.empty()) {
		set_failure(ctx, "Empty card JSON");
		return "Error: A2A card content is empty.";
	}

	nlohmann::json card;
	try {
		card = nlohmann::json::parse(json_str);
	} catch (const std::exception &e) {
		set_failure(ctx, "Invalid JSON syntax");
		std::string err_rep = std::format("### A2A Agent Card Validation Report\n\n- **Status**: ❌ INVALID\n- **Error**: JSON Syntax Error: {}\n", e.what());
		return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_validation_report", err_rep);
	}

	if (!card.is_object()) {
		set_failure(ctx, "Card root must be a JSON object");
		std::string err_rep = "### A2A Agent Card Validation Report\n\n- **Status**: ❌ INVALID\n- **Error**: Root JSON entity must be an object.\n";
		return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_validation_report", err_rep);
	}

	std::vector<std::string> errors;

	// 1. Required string field: name
	if (!card.contains("name") || !card["name"].is_string() || card["name"].get<std::string>().empty()) {
		errors.push_back("Missing or empty required string field: `name`");
	}

	// 2. Required string field: description
	if (!card.contains("description") || !card["description"].is_string() || card["description"].get<std::string>().empty()) {
		errors.push_back("Missing or empty required string field: `description`");
	}

	// 3. Optional field: version
	if (card.contains("version") && !card["version"].is_string()) {
		errors.push_back("Field `version` must be a semver string (e.g., '1.0.0').");
	}

	// 4. Optional field: input_schema
	if (card.contains("input_schema")) {
		const auto &schema = card["input_schema"];
		if (!schema.is_object() || !schema.contains("type") || schema["type"] != "object") {
			errors.push_back("Field `input_schema` must be a valid JSON Schema object with `\"type\": \"object\"`.");
		}
	}

	// 5. Optional field: output_schema
	if (card.contains("output_schema")) {
		const auto &schema = card["output_schema"];
		if (!schema.is_object() || !schema.contains("type") || schema["type"] != "object") {
			errors.push_back("Field `output_schema` must be a valid JSON Schema object with `\"type\": \"object\"`.");
		}
	}

	// 6. Optional field: endpoints
	if (card.contains("endpoints") && !card["endpoints"].is_object()) {
		errors.push_back("Field `endpoints` must be a JSON object mapping route names to URL paths.");
	}

	// 7. Optional field: skills
	if (card.contains("skills") && !card["skills"].is_array()) {
		errors.push_back("Field `skills` must be a JSON array of string skill tags.");
	}

	if (!errors.empty()) {
		set_failure(ctx, "Validation failed");
		std::string out = "### A2A Agent Card Validation Report\n\n- **Status**: ❌ INVALID\n- **Errors**:\n";
		for (const auto &err : errors) {
			out += std::format("  - {}\n", err);
		}
		return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_validation_report", out);
	}

	set_success(ctx, "Card validation succeeded");
	std::string agent_name = sanitize_card_str(card.value("name", "unnamed"));
	std::string agent_desc = sanitize_card_str(card.value("description", ""));
	std::string agent_ver = sanitize_card_str(card.value("version", "1.0.0"));

	std::string report = std::format(
	    "### A2A Agent Card Validation Report\n\n"
	    "- **Status**: ✅ VALID\n"
	    "- **Agent Name**: `{}`\n"
	    "- **Version**: `{}`\n"
	    "- **Description**: {}\n",
	    agent_name, agent_ver, agent_desc);

	if (card.contains("skills") && card["skills"].is_array()) {
		report += "- **Skills**: ";
		bool first = true;
		for (const auto &s : card["skills"]) {
			if (s.is_string()) {
				if (!first) report += ", ";
				report += "`" + sanitize_card_str(s.get<std::string>()) + "`";
				first = false;
			}
		}
		report += "\n";
	}

	if (card.contains("input_schema") && card["input_schema"].is_object()) {
		int prop_count = card["input_schema"].contains("properties") && card["input_schema"]["properties"].is_object()
				     ? static_cast<int>(card["input_schema"]["properties"].size())
				     : 0;
		report += std::format("- **Input Schema**: Valid JSON Schema ({} properties)\n", prop_count);
	}

	if (card.contains("output_schema") && card["output_schema"].is_object()) {
		int prop_count = card["output_schema"].contains("properties") && card["output_schema"]["properties"].is_object()
				     ? static_cast<int>(card["output_schema"]["properties"].size())
				     : 0;
		report += std::format("- **Output Schema**: Valid JSON Schema ({} properties)\n", prop_count);
	}

	return fs_utils::wrap_prompt_untrusted_data_tag("a2a_card_validation_report", report);
}

} // namespace tools
