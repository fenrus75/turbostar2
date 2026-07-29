#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include "agentlib/ai_agent.h"
#include "agentlib/interactions/llm_response.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include "security_review_with_agent.h"

namespace tools
{

static std::string replace_placeholder(std::string str, const std::string &placeholder, const std::string &replacement)
{
	size_t pos = 0;
	while ((pos = str.find(placeholder, pos)) != std::string::npos) {
		str.replace(pos, placeholder.length(), replacement);
		pos += replacement.length();
	}
	return str;
}

const char* const SECURITY_SCAN_PROMPT_TEMPLATE = R"raw(# CRITICAL: YOU MUST NOT ASK THE USER QUESTIONS
- Under NO circumstances are you allowed to ask the user questions, request feedback, or prompt for interactive decisions.
- Do NOT ask questions like "Would you like me to create formal code review items for any of these findings?" or similar.
- If you find security issues or findings, you MUST autonomously decide whether to file them using the `create_code_review_item` tool. Do NOT ask for permission or prompt the user to decide.
- You are executing in an automated, headless environment. There is no user to respond to you. Any question you ask will result in a failure.

# Agent Role

You are a security code review agent. Your task is to perform a thorough security code review and audit of the provided files.

Your task is NOT to fix or change source code.

# Headless Environment Constraints

You are executing in an automated, headless environment. Note the following communication rules:
- **No Interactive Input**: The user cannot see your intermediate thoughts or outputs, nor can they provide interactive feedback during execution.
- **No Questions**: You MUST NOT ask the user questions, request feedback, or prompt for interactive decisions (e.g., asking if you should create formal code review items for findings). You must make all review and filing decisions autonomously.
- **Visible Channels**: The *only* outputs visible to the user are:
  1. The contents of the configured output file (if provided). You MUST write your findings to this file using the `fs_write_file` tool (or update/modify it using `fs_replace_content`). Merely outputting findings in your text response is NOT sufficient because the user cannot see it.
  2. Any items filed or updated via the `create_code_review_item` and `update_code_review_item` tools.
  3. The final result string passed as arguments to the `agent_report_final_result` tool.

Ensure all critical security issues, descriptions, and recommended fixes are fully filed through these channels. Do not write text expecting user dialogue.

# Security Review Phases

A security code review consists of separate phases:
1. **General Code Review**: Perform a review based on your built-in expertise for security vulnerabilities (e.g., OWASP Top 10, common weaknesses).
2. **Input Validation Audit**: Identify all entry points that handle untrusted inputs. All code processing untrusted inputs must perform thorough and complete input validation.
3. **C/C++ Buffer Safety Audit**: For C/C++ files, verify all buffer accesses and string operations. Ensure any access to or manipulation of buffers provably does not exceed the allocated buffer bounds.
4. **Static Analysis Verification**: Use the provided tools `security_scan_python` (for Python source code), `security_scan_c` (for C and C++), and `security_scan_semgrep` (for all languages) to perform static analysis. Review and verify any issues found by these tools for accuracy before reporting them.

# Reporting of Findings

Assign any identified issue a severity: "nit", "low", "medium", "high", or "critical".
Always include the exact filename and line numbers in your report.

{{REPORTING_INSTRUCTIONS}}

# Files Involved in the Security Review

The following files are part of this security scan:

{{FILES_TO_REVIEW}}

You are allowed to read any related files (such as headers, imports, or referenced source code) if it helps your review, but do not report issues that exist *only* in those external files.

# Special User Instructions

{{EXTRA_INSTRUCTIONS}}

# Concluding Your Review

MANDATORY STEP: At the end of the code review task, you **must** use the `agent_report_final_result` tool to indicate your completion and report your final summary (including the total count of security issues identified).
- Reminder: You MUST NOT ask the user any questions (e.g. asking if you should create formal code review items). Do NOT wait for user input, just complete the review, write your final findings to the output file using `fs_write_file`, and invoke the `agent_report_final_result` tool.)raw";

security_review_with_agent_tool::security_review_with_agent_tool(security_review_with_agent_args args)
    : agentlib::llm_tool_action("Performing security code review"), args_(std::move(args))
{
}

bool security_review_with_agent_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	if (args_.files.empty()) {
		out_error = "Execution Error: You must specify at least one file to review.";
		return false;
	}
	return true;
}

std::string security_review_with_agent_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context available.";
	}

	auto parent = ctx.active_agent->shared_from_this();

	// 1. Resolve reviewer model
	std::string reviewer_model_id = config_manager::get_instance().get_task_model_id("code_reviewer");
	auto reviewer_model = agentlib::ai_model_registry::get_instance().get_model(reviewer_model_id);
	if (!reviewer_model) {
		reviewer_model = agentlib::ai_model_registry::get_instance().get_default_model();
	}
	if (!reviewer_model) {
		reviewer_model = parent->get_model();
	}

	// 2. Spawn subagent
	auto subagent = parent->spawn_subagent("Security Reviewer");
	if (!subagent) {
		return "Error: Failed to create security reviewer agent.";
	}

	subagent->set_role(agentlib::agent_role::reviewer);
	subagent->set_model(reviewer_model);
	subagent->set_animation_name("securityagent");
	subagent->set_exit_implicitly_on_idle(true);
	subagent->set_notify_parent_on_completion(false);

	if (!args_.output_path.empty()) {
		subagent->set_allowed_write_file(args_.output_path);
	}

	// 3. Equip with the securityagent tool family
	subagent->add_active_tool_family(":plugin:securityagent");

	// 4. Construct reporting instructions
	std::string reporting_instr;
	if (!args_.output_path.empty()) {
		reporting_instr = std::format(
		    "**After each phase**, you MUST write/append your findings to the configured output file `{}` using the `fs_write_file` tool "
		    "(or update it using the `fs_replace_content` tool).\n"
		    "CRITICAL: Do NOT just output the findings in your conversation text. You MUST use the file-writing tools (`fs_write_file`) "
		    "to actually write the findings to the file `{}`. If the file does not exist yet, create it with your initial findings. "
		    "Avoid reporting issues already identified in earlier phases as duplicates. If a previously "
		    "flagged issue is encountered in a later phase, update the existing entry in the file with any new details or findings.",
		    args_.output_path, args_.output_path);
	} else {
		reporting_instr = "**After each phase**, use the `create_code_review_item` tool to report any items found in this phase.";
	}

	// 5. Construct files to review list
	std::string files_list_str;
	std::string files_comma_str;
	for (size_t i = 0; i < args_.files.size(); ++i) {
		files_list_str += std::format("- {}\n", args_.files[i]);
		if (i > 0) {
			files_comma_str += ", ";
		}
		files_comma_str += args_.files[i];
	}

	// 6. Construct extra instructions. If the caller omitted instructions, we default to
	// directing the agent to review the target files and save the output in the configured
	// result file (if any). This provides a fallback prompt that outlines its clear objective.
	std::string extra_instr = args_.instructions;
	if (extra_instr.empty()) {
		if (!args_.output_path.empty()) {
			extra_instr = std::format("Review {} for security and place the result in `{}`.", files_comma_str, args_.output_path);
		} else {
			extra_instr = std::format("Review {} for security.", files_comma_str);
		}
	}

	// 7. Inject template variables into system prompt
	std::string system_prompt = SECURITY_SCAN_PROMPT_TEMPLATE;
	system_prompt = replace_placeholder(system_prompt, "{{REPORTING_INSTRUCTIONS}}", reporting_instr);
	system_prompt = replace_placeholder(system_prompt, "{{FILES_TO_REVIEW}}", files_list_str);
	system_prompt = replace_placeholder(system_prompt, "{{EXTRA_INSTRUCTIONS}}", extra_instr);

	// 8. Inject prompts
	subagent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());
	subagent->inject_context("system", system_prompt);

	std::string task_prompt = std::format("Please perform a security code review on the following files:\n{}", files_list_str);
	task_prompt += std::format("\nSpecific focus / instructions:\n{}", extra_instr);
	subagent->submit_prompt(task_prompt);

	// 9. Synchronously wait for the subagent to finish
	auto old_status = parent->get_status();
	parent->set_status(agentlib::agent_status::waiting, subagent->get_id());
	subagent->wait_until_idle();
	parent->set_status(old_status);

	set_success(ctx);

	if (subagent->get_status() == agentlib::agent_status::error) {
		return std::format("Security Reviewer Agent '{}' encountered an error during execution.", subagent->get_name());
	}

	if (subagent->has_final_result()) {
		return subagent->get_final_result();
	}

	const auto &interactions = subagent->get_interactions();
	for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
		auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
		if (res) {
			return res->get_text();
		}
	}

	return std::format("Security Reviewer Agent '{}' completed successfully, but no final response was found.", subagent->get_name());
}

} // namespace tools
