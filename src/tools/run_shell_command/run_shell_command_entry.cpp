#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include "../../agentlib/ai_agent.h"
#include "../../agentlib/tool_context.h"
#include "../../config_manager.h"
#include "../../fs_utils.h"
#include "../terminal_command_runner.h"
#include "run_shell_command.h"

namespace tools
{

// In-memory session permission manager
static std::mutex g_perms_mutex;
static std::unordered_map<std::string, char> g_command_perms; // 'A' = always allow, 'D' = deny always

/*
 * g_log_mutex protects the append-only command log file writes. The run_shell_command
 * tool can be invoked concurrently (async worker thread + multiple agents), so all appends
 * must be serialized to avoid interleaved/corrupt records and to make the shared
 * std::localtime buffer access thread-safe.
 */
static std::mutex g_log_mutex;

// Returns the path of the dedicated, append-only shell-command log file. Commands are
// logged here (when cfg.is_log_shell_commands() is enabled) so that over time we can
// build a corpus for the "you could have used <tool>" hint analysis.
static std::filesystem::path get_shell_command_log_path()
{
	const char *home = std::getenv("HOME");
	std::filesystem::path base = home ? std::filesystem::path(home) / ".cache" / "turbostar"
					: std::filesystem::path(".cache") / "turbostar";
	std::error_code ec;
	std::filesystem::create_directories(base, ec); // best-effort; ignore errors
	return base / "shell_commands.log";
}

// Escapes a value for the tab-separated, one-record-per-line log: tabs, newlines,
// carriage returns, and backslashes are replaced with backslash escapes so a multi-line
// command or a cwd containing these characters cannot break the record format or forge
// extra records.
static std::string escape_log_field(const std::string &value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (char c : value) {
		switch (c) {
		case '\\': escaped += "\\\\"; break;
		case '\t': escaped += "\\t"; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		default: escaped += c; break;
		}
	}
	return escaped;
}

// Appends a single executed-command record (tab-separated) to the command log file.
// Columns: date-time | approval label | working directory | command.
// All fields are escaped so the corpus the "you could have used <tool>" analysis builds
// stays well-formed. This is a best-effort telemetry side effect and must never throw: it
// runs on the already-approved path, so any failure here must not abort the real command.
static void append_command_to_log(const std::string &approval_label, const std::string &command)
{
	std::lock_guard<std::mutex> lock(g_log_mutex);

	const std::time_t now = std::time(nullptr);
	char time_buf[32];
	if (const std::tm *tm_ptr = std::localtime(&now); tm_ptr != nullptr) {
		std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_ptr);
	} else {
		std::snprintf(time_buf, sizeof(time_buf), "unknown");
	}

	// current_path() can throw if the CWD was deleted; never let a logging side effect
	// fail an approved command.
	std::error_code ec;
	std::filesystem::path cwd = std::filesystem::current_path(ec);
	std::string cwd_str = ec ? std::string("(unknown)") : cwd.string();

	std::ofstream log(get_shell_command_log_path(), std::ios::app);
	if (!log.is_open()) {
		return;
	}
	log << time_buf << '\t' << escape_log_field(approval_label) << '\t' << escape_log_field(cwd_str) << '\t'
	    << escape_log_field(command) << '\n';
}

run_shell_command_tool::run_shell_command_tool(run_shell_command_args args) : args_(std::move(args))
{
	interaction_ = std::make_shared<agentlib::interaction_terminal>("Shell Command", "Executing...");
}

std::shared_ptr<agentlib::agent_interaction> run_shell_command_tool::get_interaction() const
{
	return interaction_;
}

bool run_shell_command_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string run_shell_command_tool::execute(agentlib::tool_context &ctx)
{
	if (ctx.doc_provider) {
		ctx.doc_provider->save_all_documents();
	}

	char rule = '?';
	{
		std::lock_guard<std::mutex> lock(g_perms_mutex);
		auto it = g_command_perms.find(args_.command);
		if (it != g_command_perms.end()) {
			rule = it->second;
		}
	}

	if (rule == 'D') {
		return "Error: Permission denied by user to run this command (Blacklisted).";
	}

	// Measure how long the user takes to approve, so we can report it back to the LLM.
	// We only time an actual prompt; a pre-approved ("Always") command skips the prompt.
	std::string approval_label = "pre-approved";
	std::string approval_note;
	if (rule != 'A') {
		if (!ctx.queue) {
			return "Error: No event queue available to prompt the user for permission.";
		}

		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();

		editor_event ev;
		ev.type = event_type::prompt_user;
		ev.payload = "Agent wants to execute the following shell command:\n\n" + args_.command + "\n\nAllow execution?";
		ev.prompt_options = {"Once", "Always", "Deny Always", "Deny"};
		ev.prompt_promise = promise;

		const auto t0 = std::chrono::steady_clock::now();
		ctx.queue->push(ev);

		std::string response;
		try {
			response = future.get();
		} catch (const std::exception &e) {
			return std::string("Error: Failed to get user response - ") + e.what();
		}
		const auto t1 = std::chrono::steady_clock::now();
		const double approve_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

		if (response == "Deny") {
			return "Error: Permission denied by user for this request.";
		} else if (response == "Deny Always") {
			std::lock_guard<std::mutex> lock(g_perms_mutex);
			g_command_perms[args_.command] = 'D';
			return "Error: Permission denied by user (Blacklisted).";
		} else if (response == "Always") {
			std::lock_guard<std::mutex> lock(g_perms_mutex);
			g_command_perms[args_.command] = 'A';
			approval_label = "approved";
		} else if (response != "Once") {
			return "Error: Unknown response from user.";
		} else {
			approval_label = "approved";
		}

		// Report approval latency back to the LLM so it learns how disruptive a
		// run_shell_command approval round-trip is (encouraging use of direct tools).
		approval_note = std::format(" (approved by user in {:.1f}s)", approve_ms / 1000.0);
	}

	// Permission granted
	if (config_manager::get_instance().is_log_shell_commands()) {
		append_command_to_log(approval_label, args_.command);
	}

	auto runner = std::make_shared<terminal_command_runner>(interaction_, ctx.trigger_ui_update);
	runner->apply_strict_agent_profile();
	runner->set_allow_display(config_manager::get_instance().is_shell_display_access());
	runner->set_enable_crash_catcher(true);
	runner->set_project_dir(ctx.fs_security.get_working_directory().string());
	runner->set_timeout(args_.timeout);

	bool async_enabled = args_.is_async;
	if (ctx.active_agent && !ctx.active_agent->is_mutation_possible()) {
		async_enabled = false;
	}

	if (async_enabled) {
		std::weak_ptr<agentlib::ai_agent> weak_agent;
		if (ctx.active_agent) {
			// Need to convert raw ptr to shared_ptr if possible. Oh wait, ai_agent inherits from enable_shared_from_this.
			weak_agent = ctx.active_agent->shared_from_this();
		}
		std::string captured_tool_call_id = ctx.tool_call_id;

		std::thread([runner, cmd = args_.command, weak_agent, captured_tool_call_id]() {
			runner->execute(cmd);

			if (auto agent = weak_agent.lock()) {
				std::string output = runner->get_final_output();
				if (output.empty()) {
					output = "Command finished successfully with no output.";
				}

				if (output.length() > 20000) {
					output = output.substr(output.length() - 20000);
					output = "\n...[output truncated due to length]...\n" + output;
				}

				std::string formatted_injection = "\n\n--- ASYNC COMMAND COMPLETED ---\n```\n" + output + "\n```";
				agent->replace_tool_result(captured_tool_call_id, formatted_injection);

				// Wake up the LLM
				agent->inject_context("system",
						      "The background task 'run_shell_command' (" + cmd +
							  ") has completed. I updated your previous tool result with the output.",
						      true);
			}
		}).detach();
		return "Command started in background. The output will be injected here when it completes." + approval_note;
	}

	// Execute directly: command_runner automatically escapes and wraps it via systemd-run ... -- bash -c '...'
	runner->execute(args_.command);

	std::string output = runner->get_final_output();

	if (output.empty()) {
		output = "Command finished successfully with no output.";
		if (interaction_) {
			interaction_->set_text(output);
			if (ctx.trigger_ui_update) {
				ctx.trigger_ui_update();
			}
		}
	}

	// Cap output at 20,000 characters to protect context window
	if (output.length() > 20000) {
		output = output.substr(output.length() - 20000);
		output = "\n...[output truncated due to length]...\n" + output;
	}

	return "```\n" + output + "\n```" + approval_note;
}

} // namespace tools