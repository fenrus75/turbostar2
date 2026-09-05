#include "agentlib/tool_registry.h"
#include "resolve_code_review_item.h"

namespace tools {

bool resolve_code_review_item_validator::validate_args_impl(
	const nlohmann::json& raw_json,
	const agentlib::tool_context& /*ctx*/,
	std::string& out_error
) const {
	try {
		std::vector<int> target_ids;

		if (raw_json.contains("item_ids") && raw_json["item_ids"].is_array()) {
			for (const auto &val : raw_json["item_ids"]) {
				if (!val.is_number_integer() || val.get<int>() <= 0) {
					out_error = "Each item ID in 'item_ids' must be a positive integer greater than 0.";
					return false;
				}
				target_ids.push_back(val.get<int>());
			}
		} else if (raw_json.contains("ids") && raw_json["ids"].is_array()) {
			for (const auto &val : raw_json["ids"]) {
				if (!val.is_number_integer() || val.get<int>() <= 0) {
					out_error = "Each item ID in 'ids' must be a positive integer greater than 0.";
					return false;
				}
				target_ids.push_back(val.get<int>());
			}
		} else if (raw_json.contains("item_id") && raw_json["item_id"].is_number_integer()) {
			int val = raw_json["item_id"].get<int>();
			if (val <= 0) {
				out_error = "item_id must be a positive integer greater than 0.";
				return false;
			}
			target_ids.push_back(val);
		} else if (raw_json.contains("id") && raw_json["id"].is_number_integer()) {
			int val = raw_json["id"].get<int>();
			if (val <= 0) {
				out_error = "id must be a positive integer greater than 0.";
				return false;
			}
			target_ids.push_back(val);
		} else {
			out_error = "Missing required argument: 'item_id' (or 'item_ids' array).";
			return false;
		}

		if (target_ids.empty()) {
			out_error = "At least one item ID must be provided.";
			return false;
		}

		if (!raw_json.contains("commit_hash") || !raw_json["commit_hash"].is_string()) {
			out_error = "Missing required argument 'commit_hash'.";
			return false;
		}

		std::string commit_hash = raw_json["commit_hash"].get<std::string>();
		if (commit_hash.empty()) {
			out_error = "Commit hash must not be empty.";
			return false;
		}

		if (commit_hash.length() < 7 || commit_hash.length() > 40) {
			out_error = "Validation Error: commit_hash must be between 7 and 40 hexadecimal characters.";
			return false;
		}

		for (char c : commit_hash) {
			if (!std::isxdigit(static_cast<unsigned char>(c))) {
				out_error = "Validation Error: commit_hash contains non-hexadecimal characters.";
				return false;
			}
		}

		args_.item_ids = std::move(target_ids);
		args_.commit_hash = commit_hash;
		return true;
	} catch (const std::exception& e) {
		out_error = "Invalid arguments: " + std::string(e.what());
		return false;
	}
}

std::unique_ptr<agentlib::llm_tool> resolve_code_review_item_validator::create_tool_impl(const nlohmann::json& /*raw_json*/) const {
	return std::make_unique<resolve_code_review_item_tool>(args_);
}

REGISTER_TOOL(resolve_code_review_item_validator)

} // namespace tools
