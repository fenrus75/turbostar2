#include "markdown_extract_tool.h"
#include "markdown_extract_agent.h"
#include "agentlib/ai_agent.h"
#include "agentlib/interactions/llm_response.h"
#include "agentlib/virtual_file_system.h"
#include "config_manager.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace tools {

static std::string replace_placeholder(std::string str, const std::string &placeholder, const std::string &replacement)
{
	size_t pos = 0;
	while ((pos = str.find(placeholder, pos)) != std::string::npos) {
		str.replace(pos, placeholder.length(), replacement);
		pos += replacement.length();
	}
	return str;
}

markdown_extract_tool::markdown_extract_tool(markdown_extract_args args)
	: llm_tool_action("Extracting structured information from Markdown")
	, args_(std::move(args))
{
}

bool markdown_extract_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.active_agent) {
		out_error = "Execution Error: No active agent context available.";
		return false;
	}
	return true;
}

std::string markdown_extract_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.active_agent) {
		return "Error: No active agent context available.";
	}

	auto parent = ctx.active_agent->shared_from_this();

	// 1. Resolve extractor model
	std::string model_id = config_manager::get_instance().get_task_model_id("markdown_extractor");
	auto model = agentlib::ai_model_registry::get_instance().get_model(model_id);
	if (!model) {
		model = agentlib::ai_model_registry::get_instance().get_default_model();
	}
	if (!model) {
		model = parent->get_model();
	}

	// 2. Determine document line count & target path
	std::string target_p = !args_.safe_path.empty() ? args_.safe_path : args_.path;
	std::string target_out = !args_.safe_output_path.empty() ? args_.safe_output_path : args_.output_path;

	std::string line_count_str = "0";
	if (target_p.find("://") != std::string::npos) {
		auto vfs = ctx.fs_security.get_vfs();
		agentlib::virtual_file_system local_vfs;
		if (!vfs) {
			vfs = &local_vfs;
		}
		if (vfs->exists(target_p)) {
			auto content_opt = vfs->read_file(target_p);
			if (content_opt && *content_opt) {
				std::string_view sv = (*content_opt)->view();
				size_t lines = std::count(sv.begin(), sv.end(), '\n') + 1;
				line_count_str = std::to_string(lines);
			}
		}
	} else {
		line_count_str = fs_utils::count_lines_in_file(target_p);
		if (line_count_str.empty()) {
			line_count_str = "0";
		}
	}

	// 3. Spawn subagent
	auto subagent = parent->spawn_subagent("Markdown Extractor");
	if (!subagent) {
		return "Error: Failed to create Markdown Extractor subagent.";
	}

	subagent->set_role(agentlib::agent_role::summarizer);
	subagent->set_model(model);
	subagent->set_exit_implicitly_on_idle(true);
	subagent->set_notify_parent_on_completion(false);

	if (!target_out.empty()) {
		subagent->set_allowed_write_file(target_out);
		subagent->set_read_only(false);
	}

	// 4. Inject prompt template variables safely using XML data tag wrappers
	std::string safe_query_tag = fs_utils::wrap_prompt_untrusted_data_tag("user_query", args_.query);
	std::string safe_file_tag = fs_utils::wrap_prompt_untrusted_data_tag("target_file", target_p);

	std::string system_prompt = markdown_extract_agent_md;
	system_prompt = replace_placeholder(system_prompt, "@@filename@@", safe_file_tag);
	system_prompt = replace_placeholder(system_prompt, "@@lines@@", line_count_str);
	system_prompt = replace_placeholder(system_prompt, "@@query@@", safe_query_tag);
	system_prompt = replace_placeholder(system_prompt, "@@output_path@@", target_out.empty() ? "None" : target_out);

	subagent->inject_context("system", project_manager::get_instance().get_project_knowledge_prompt());
	subagent->inject_context("system", system_prompt);

	std::string task_prompt = std::format("Please inspect target document ({} lines) and extract all relevant information for query:\n{}\n{}",
					      line_count_str, safe_file_tag, safe_query_tag);
	subagent->submit_prompt(task_prompt);

	// 5. Handle Async vs Sync Execution
	if (args_.is_async) {
		set_success(ctx, "Markdown extraction dispatched to background subagent");
		return std::format("### Markdown Extraction Dispatched (Async)\n\n"
				   "- **Document**: `{}`\n"
				   "- **Query**: `{}`\n"
				   "- **Subagent**: `{}`\n",
				   args_.path, args_.query, subagent->get_name());
	}

	// Synchronous execution
	auto old_status = parent->get_status();
	parent->set_status(agentlib::agent_status::waiting, subagent->get_id());
	subagent->wait_until_idle();
	parent->set_status(old_status);

	set_success(ctx);

	if (subagent->get_status() == agentlib::agent_status::error) {
		return std::format("Markdown Extractor subagent encountered an error while processing `{}`.", args_.path);
	}

	std::string extracted_text;
	if (subagent->has_final_result()) {
		extracted_text = subagent->get_final_result();
	} else {
		const auto &interactions = subagent->get_interactions();
		for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {
			auto res = std::dynamic_pointer_cast<agentlib::interaction_llm_response>(*it);
			if (res) {
				extracted_text = res->get_text();
				break;
			}
		}
	}

	if (extracted_text.empty()) {
		return std::format("Markdown Extractor completed successfully, but no content was extracted for query `{}`.", args_.query);
	}

	// 6. Write to target_out if requested
	if (!target_out.empty()) {
		bool write_ok = false;
		if (target_out.find("://") != std::string::npos) {
			auto vfs = ctx.fs_security.get_vfs();
			agentlib::virtual_file_system local_vfs;
			if (!vfs) {
				vfs = &local_vfs;
			}
			std::string res_desc = vfs->write_file(target_out, extracted_text.data(), extracted_text.size());
			write_ok = !res_desc.empty();
		} else {
			std::ofstream ofs(target_out, std::ios::binary);
			if (ofs.is_open()) {
				ofs << extracted_text;
				write_ok = true;
			}
		}

		if (write_ok) {
			return std::format("### Markdown Extraction Complete\n\nExtracted content for query saved to `{}`.\n", target_out);
		} else {
			return std::format("### Markdown Extraction Complete (File Write Failed)\n\n"
					   "Failed to write output to `{}`. Extracted Content:\n\n{}",
					   target_out, extracted_text);
		}
	}

	return extracted_text;
}

} // namespace tools
