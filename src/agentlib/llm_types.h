#pragma once
#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace agentlib {

struct function_call {
    std::string name;
    std::string arguments;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(function_call, name, arguments);

struct tool_call {
    std::string id;
    std::string type;
    function_call function;
    std::optional<std::string> signature;
};

inline void to_json(nlohmann::json& j, const tool_call& p) {
    j = nlohmann::json{{"id", p.id}, {"type", p.type}, {"function", p.function}};
    if (p.signature) j["signature"] = *p.signature;
}

inline void from_json(const nlohmann::json& j, tool_call& p) {
    if (j.contains("id") && !j["id"].is_null()) p.id = j["id"].get<std::string>();
    if (j.contains("type") && !j["type"].is_null()) p.type = j["type"].get<std::string>();
    if (j.contains("function") && !j["function"].is_null()) p.function = j["function"].get<function_call>();
    if (j.contains("signature") && !j["signature"].is_null()) p.signature = j["signature"].get<std::string>();
}

struct message {
    std::string role;
    std::string content;
    std::optional<std::string> reasoning_content;
    std::optional<std::string> name;
    std::optional<std::string> tool_call_id;
    std::optional<std::vector<tool_call>> tool_calls;

    // Transient episode mapping fields
    std::string episode_id;
    int episode_level{-1}; // -1 if not a paged-in episode turn

    // Local DNN training / temporal metadata fields
    long long timestamp{0}; // Unix timestamp in seconds
    long long duration_ms{0}; // Duration in milliseconds
};

inline void to_json(nlohmann::json& j, const message& p) {
    j = nlohmann::json{{"role", p.role}, {"content", p.content}};
    if (p.reasoning_content) j["reasoning_content"] = *p.reasoning_content;
    if (p.name) j["name"] = *p.name;
    if (p.tool_call_id) j["tool_call_id"] = *p.tool_call_id;
    if (p.tool_calls) j["tool_calls"] = *p.tool_calls;
    if (!p.episode_id.empty()) {
        j["episode_id"] = p.episode_id;
        j["episode_level"] = p.episode_level;
    }
    if (p.timestamp != 0) j["timestamp"] = p.timestamp;
    if (p.duration_ms != 0) j["duration_ms"] = p.duration_ms;
}

inline void from_json(const nlohmann::json& j, message& p) {
    j.at("role").get_to(p.role);
    if (j.contains("content") && !j["content"].is_null()) {
        j.at("content").get_to(p.content);
    }
    if (j.contains("reasoning_content") && !j["reasoning_content"].is_null()) {
        p.reasoning_content = j["reasoning_content"].get<std::string>();
    }
    if (j.contains("name")) p.name = j.at("name").get<std::string>();
    if (j.contains("tool_call_id")) p.tool_call_id = j.at("tool_call_id").get<std::string>();
    if (j.contains("tool_calls")) p.tool_calls = j.at("tool_calls").get<std::vector<tool_call>>();
    if (j.contains("episode_id")) {
        p.episode_id = j.at("episode_id").get<std::string>();
        p.episode_level = j.value("episode_level", -1);
    } else {
        p.episode_id = "";
        p.episode_level = -1;
    }
    p.timestamp = j.value("timestamp", 0LL);
    p.duration_ms = j.value("duration_ms", 0LL);
}

struct llm_usage {
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    int cached_tokens{0};
};

struct llm_chat_response {
    message msg;
    llm_usage usage;
    std::string model;
};

struct chat_delta {
    std::string role;
    std::string content;
    std::string reasoning_content;
    std::optional<std::vector<tool_call>> tool_calls;
    bool is_final{false};
    llm_usage usage;
};

inline void normalize_tool_call(tool_call &call)
{
	std::string alias = call.function.name;

	// Normalize name
	std::string official_name = alias;
	if (alias == "read_file" || alias == "view_file" || alias == "cat") {
		official_name = "fs_read_lines";
	} else if (alias == "grep" || alias == "search_grep" || alias == "find_in_files") {
		official_name = "fs_grep_files";
	} else if (alias == "list_dir") {
		official_name = "fs_list_dir";
	} else if (alias == "mkdir" || alias == "create_directory") {
		official_name = "fs_mkdir";
	} else if (alias == "run_tests") {
		official_name = "fs_run_tests";
	} else if (alias == "git_diff") {
		official_name = "git_diff_unstaged";
	}

	if (official_name == alias) {
		return; // No normalization needed
	}

	call.function.name = official_name;

	// Parse arguments JSON
	try {
		nlohmann::json args = nlohmann::json::parse(call.function.arguments);
		if (!args.is_object()) {
			return;
		}

		if (official_name == "fs_read_lines") {
			// Find a path key and map it to "path"
			std::string path_val;
			for (const auto &key : {"file", "file_path", "filepath", "filename", "path"}) {
				if (args.contains(key) && args[key].is_string()) {
					path_val = args[key].get<std::string>();
					break;
				}
			}
			nlohmann::json new_args = nlohmann::json::object();
			if (!path_val.empty()) {
				new_args["path"] = path_val;
			} else {
				// preserve whatever was there if none found
				new_args = args;
			}
			if (args.contains("start_line")) new_args["start_line"] = args["start_line"];
			if (args.contains("end_line")) new_args["end_line"] = args["end_line"];
			args = new_args;
		} else if (official_name == "fs_grep_files") {
			std::string pattern_val;
			for (const auto &key : {"pattern", "query", "search_query", "regex", "text"}) {
				if (args.contains(key) && args[key].is_string()) {
					pattern_val = args[key].get<std::string>();
					break;
				}
			}
			std::string dir_path_val;
			for (const auto &key : {"dir_path", "path", "dir", "directory"}) {
				if (args.contains(key) && args[key].is_string()) {
					dir_path_val = args[key].get<std::string>();
					break;
				}
			}
			nlohmann::json new_args = nlohmann::json::object();
			if (!pattern_val.empty()) {
				new_args["pattern"] = pattern_val;
			}
			if (!dir_path_val.empty()) {
				new_args["dir_path"] = dir_path_val;
			}
			for (const auto &k : {"include_ext", "max_results", "context_lines"}) {
				if (args.contains(k)) {
					new_args[k] = args[k];
				}
			}
			args = new_args;
		} else if (official_name == "fs_list_dir" || official_name == "fs_mkdir" || official_name == "git_diff_unstaged") {
			std::string path_val;
			for (const auto &key : {"path", "file", "file_path", "filepath", "dir", "directory", "dir_path"}) {
				if (args.contains(key) && args[key].is_string()) {
					path_val = args[key].get<std::string>();
					break;
				}
			}
			nlohmann::json new_args = nlohmann::json::object();
			if (!path_val.empty()) {
				new_args["path"] = path_val;
			} else if (official_name == "git_diff_unstaged") {
				new_args["path"] = "."; // default for git_diff
			}
			args = new_args;
		}

		call.function.arguments = args.dump();
	} catch (...) {
		// Ignore parse failures, let validator handle bad JSON format
	}
}

} // namespace agentlib
