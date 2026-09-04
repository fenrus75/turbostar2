#include "agent_write_to_run.h"
#include "fs_utils.h"
#include "utf8.h"
#include <chrono>
#include <thread>
#include <numeric>

namespace tools
{

static std::string sanitize_pty_output(const std::string &raw)
{
	std::string out = utf8::sanitize_terminal_output(raw);
	std::string clean;
	clean.reserve(out.size());
	for (char c : out) {
		if (c == '\n' || c == '\r' || c == '\t' || (static_cast<unsigned char>(c) >= 32 && c != 127)) {
			clean += c;
		}
	}
	return clean;
}

bool agent_write_to_run_tool::validate_runtime(const agentlib::tool_context &ctx, std::string &out_error) const
{
	if (!ctx.doc_provider) {
		out_error = "Execution Error: No document provider context available.";
		return false;
	}
	return true;
}

std::string agent_write_to_run_tool::execute(agentlib::tool_context &ctx)
{
	if (!ctx.doc_provider) {
		set_failure(ctx, "Internal Error: document provider is not available");
		return "Error: Internal engine type mismatch.";
	}

	if (args_.output) {
		ctx.doc_provider->set_run_recording(args_.run_id, true);
	}

	if (ctx.doc_provider->write_to_run(args_.run_id, args_.data)) {
		if (args_.output) {
			auto start_time = std::chrono::steady_clock::now();
			while (true) {
				int64_t age = ctx.doc_provider->get_run_last_modified_age(args_.run_id);
				if (age >= 250) {
					break;
				}
				auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
				if (elapsed_ms >= 3000) {
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}
			ctx.doc_provider->set_run_recording(args_.run_id, false);
			std::vector<std::string> recorded = ctx.doc_provider->get_run_recorded_data(args_.run_id);
			std::string output_str = std::accumulate(recorded.begin(), recorded.end(), std::string{});
			output_str = sanitize_pty_output(output_str);
			if (output_str.length() > 20000) {
				output_str.resize(20000);
				output_str += "\n\n*(Output truncated at 20,000 characters)*\n";
			}
			set_success(ctx, "Wrote " + std::to_string(args_.data.length()) + " bytes to run_id " + std::to_string(args_.run_id) + " and captured output.");
			return fs_utils::wrap_prompt_untrusted_data_tag("agent_write_to_run_result", output_str);
		} else {
			set_success(ctx, "Wrote " + std::to_string(args_.data.length()) + " bytes to run_id " + std::to_string(args_.run_id));
			return fs_utils::wrap_prompt_untrusted_data_tag("agent_write_to_run_result", "Successfully wrote input data to the PTY master.");
		}
	} else {
		if (args_.output) {
			ctx.doc_provider->set_run_recording(args_.run_id, false);
		}
		set_failure(ctx, "Failed to write data to run_id " + std::to_string(args_.run_id));
		return "Error: Run not found or process is not alive/writable.";
	}
}

} // namespace tools
