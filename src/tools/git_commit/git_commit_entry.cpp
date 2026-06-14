#include <filesystem>
#include <fstream>
#include <format>
#include <system_error>
#include "../../codereview_manager.h"
#include "../../fs_utils.h"
#include "git_commit.h"

namespace tools
{

git_commit_tool::git_commit_tool(std::string message) : llm_tool_action("Committing staged changes"), message_(std::move(message))
{
}

bool git_commit_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string git_commit_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	// Check if there is anything to commit first
	std::string check_cmd = "git --no-pager diff --staged --quiet";
	std::string check_output = fs_utils::execute_command_sync(check_cmd);

	// If diff --quiet exits with 0, there are NO staged changes.
	// However, execute_command_sync doesn't return the exit code directly.
	// Instead we can use git status --porcelain.
	std::string status_cmd = "git --no-pager status --porcelain";
	std::string status_output = fs_utils::execute_command_sync(status_cmd);

	bool has_staged = false;
	std::stringstream ss(status_output);
	std::string line;
	while (std::getline(ss, line)) {
		if (line.starts_with("Process exited with code")) {
			continue;
		}
		if (line.length() >= 2) {
			char staged_status = line[0];
			if (staged_status != ' ' && staged_status != '?') {
				has_staged = true;
				break;
			}
		}
	}

	if (!has_staged) {
		set_failure(ctx, "Nothing to commit");
		return "Failed: No staged changes found. Use git_add to stage files first.";
	}

	// Write commit message to a temporary file to avoid shell injection
	std::filesystem::path temp_dir = std::filesystem::path(fs_utils::get_project_tmp_dir());
	std::filesystem::path msg_file = temp_dir / ("commit_msg_" + std::to_string(std::hash<std::string>{}(message_)) + ".txt");

	std::ofstream out(msg_file);
	if (!out) {
		set_failure(ctx, "Internal error");
		return "Failed to create temporary commit message file.";
	}
	out << message_;
	out.close();

	std::string output = fs_utils::execute_command_sync("git commit -F {}", msg_file);

	// Clean up
	std::error_code ec;
	std::filesystem::remove(msg_file, ec);

	if (output.find("fatal:") != std::string::npos || output.find("error:") != std::string::npos) {
		set_failure(ctx, "Git commit failed");
		return "Failed to commit:\n```\n" + output + "\n```";
	}

	set_success(ctx, "Commit created");
	std::string ret_msg = std::format("Successfully created commit:\n```\n{}\n```", output);

	// Check if there are outstanding (new or confirmed) code review items
	auto all_items = codereview_manager::get_instance().list_code_review_items("", "", false);
	std::vector<review_item> outstanding;
	for (const auto &item : all_items) {
		if (item.state == "new" || item.state == "confirmed") {
			outstanding.push_back(item);
		}
	}

	if (!outstanding.empty()) {
		std::string commit_hash = "unknown";
		std::string hash_output = fs_utils::execute_command_sync("git rev-parse HEAD");
		std::stringstream hash_ss(hash_output);
		std::string first_line;
		if (std::getline(hash_ss, first_line)) {
			while (!first_line.empty() && (first_line.back() == '\r' || first_line.back() == '\n' || std::isspace(first_line.back()))) {
				first_line.pop_back();
			}
			if (!first_line.empty() && !first_line.starts_with("Process exited with code")) {
				commit_hash = first_line;
			}
		}

		ret_msg += std::format("\n\n### Outstanding Code Review Items Reminder:\n"
				       "There are outstanding code review items remaining in the project. If any of these items were addressed/fixed "
				       "in this commit (hash: {}), please call the `resolve_code_review_item` tool to transition their state to \"resolved\".\n"
				       "Active items:\n", commit_hash);
		for (const auto &item : outstanding) {
			ret_msg += std::format("- #{} ({}): {} [File: {}:{}]\n", item.id, item.state, item.summary, item.filename, item.line_number);
		}
	}

	return ret_msg;
}

} // namespace tools
