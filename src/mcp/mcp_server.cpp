#include "mcp_server.h"
#include <array>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../command_runner.h"
#include "../config_manager.h"
#include "../event_logger.h"
#include "../fs_utils.h"
#include "mcp_manager.h"

namespace agentlib
{

class mcp_command_runner : public command_runner
{
      protected:
	void on_output_chunk(const std::string &) override
	{
	}
};

mcp_server::~mcp_server()
{
	stop();
}

void mcp_server::auto_detect_type()
{
	mcp_type_ = "other";
	if (command_ == "uv" || command_ == "uvx") {
		mcp_type_ = "uv";
	} else if (command_ == "npx" || command_ == "npm" || command_ == "node") {
		mcp_type_ = "npm";
	} else if (command_ == "python" || command_ == "python3") {
		mcp_type_ = "python";
	} else {
		for (const auto &arg : args_) {
			if (arg.ends_with(".py")) {
				mcp_type_ = "python";
				break;
			} else if (arg.ends_with(".js") || arg.ends_with(".mjs") || arg.ends_with(".ts")) {
				mcp_type_ = "npm";
				break;
			}
		}
	}
}

std::string mcp_server::get_python_script_path() const
{
	if (mcp_type_ != "python") {
		return "";
	}
	if (command_.ends_with(".py") || command_.find(".py") != std::string::npos) {
		return command_;
	}
	for (const auto &arg : args_) {
		if (arg.ends_with(".py") || arg.find(".py") != std::string::npos) {
			return arg;
		}
	}
	return "";
}

static std::string expand_tilde(const std::string &path)
{
	if (path.starts_with("~/")) {
		const char *home = std::getenv("HOME");
		if (home) {
			return std::string(home) + path.substr(1);
		}
	}
	return path;
}

bool mcp_server::run_bandit_scan(std::string &scan_output) const
{
	bool bandit_installed = (access("/usr/bin/bandit", X_OK) == 0);
	if (!bandit_installed) {
		return true;
	}
	std::string script_path = get_python_script_path();
	if (script_path.empty()) {
		return true;
	}
	script_path = expand_tilde(script_path);

	sync_command_runner bandit_runner;
	bandit_runner.apply_build_profile();
	scan_output = bandit_runner.execute_and_get_output("bandit --severity-level=high {} 2>&1", script_path);
	int exit_code = bandit_runner.get_exit_code();
	return (exit_code == 0);
}

bool mcp_server::start()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (is_running()) {
		return true;
	}

	if (mcp_type_ == "python") {
		std::string scan_output;
		if (!run_bandit_scan(scan_output)) {
			event_logger::get_instance().log("Security Validation Failed: Bandit detected high-severity issues in Python MCP server '{}':\n{}", name_, scan_output);
			return false;
		}
	}

	// 1. Create pipes
	int stdin_pipe[2];
	int stdout_pipe[2];
	int stderr_pipe[2];

	if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
		event_logger::get_instance().log("MCP server '{}' start failed: pipe creation failed.", name_);
		return false;
	}

	// 2. Build command
	std::string raw_command = "";
	for (const auto &[key, val] : env_) {
		raw_command += fs_utils::escape_shell_arg(key) + "=" + fs_utils::escape_shell_arg(val) + " ";
	}
	raw_command += fs_utils::escape_shell_arg(command_);
	for (const auto &arg : args_) {
		raw_command += " " + fs_utils::escape_shell_arg(arg);
	}

	std::string final_command;
	if (is_system_) {
		final_command = raw_command;
	} else {
		mcp_command_runner runner;
		runner.apply_strict_agent_profile();
		runner.set_network_access(false);
		final_command = runner.build_command(raw_command);
	}

	// 3. Fork
	pid_t pid = fork();
	if (pid < 0) {
		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);
		event_logger::get_instance().log("MCP server '{}' start failed: fork failed.", name_);
		return false;
	}

	if (pid == 0) {
		// Child process
		dup2(stdin_pipe[0], STDIN_FILENO);
		dup2(stdout_pipe[1], STDOUT_FILENO);
		dup2(stderr_pipe[1], STDERR_FILENO);

		close(stdin_pipe[0]);
		close(stdin_pipe[1]);
		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);

		// Separate process group so we can terminate it and children
		setpgid(0, 0);

		execl("/bin/sh", "sh", "-c", final_command.c_str(), nullptr);
		_exit(127);
	}

	// Parent process
	pid_ = pid;
	stdin_fd_ = stdin_pipe[1];
	stdout_fd_ = stdout_pipe[0];
	stderr_fd_ = stderr_pipe[0];

	close(stdin_pipe[0]);
	close(stdout_pipe[1]);
	close(stderr_pipe[1]);

	reader_running_ = true;
	reader_thread_ = std::thread(&mcp_server::reader_loop, this);
	stderr_thread_ = std::thread(&mcp_server::stderr_loop, this);

	event_logger::get_instance().log("MCP server '{}' spawned with PID {}.", name_, pid_);

	// 4. MCP Initialization Handshake
	try {
		nlohmann::json init_params = {{"protocolVersion", "2024-11-05"},
					      {"capabilities", nlohmann::json::object()},
					      {"clientInfo", {{"name", "turbostar"}, {"version", "1.0.0"}}}};

		auto init_res = send_request("initialize", init_params);
		if (init_res.contains("error")) {
			event_logger::get_instance().log("MCP server '{}' initialization failed: {}", name_, init_res["error"].dump());
			stop();
			return false;
		}

		send_notification("notifications/initialized", nlohmann::json::object());

		// Retrieve discovered tools list
		auto list_res = send_request("tools/list", nlohmann::json::object());
		if (list_res.contains("result") && list_res["result"].contains("tools") && list_res["result"]["tools"].is_array()) {
			std::vector<mcp_tool> tools;
			for (const auto &t_json : list_res["result"]["tools"]) {
				mcp_tool tool;
				tool.name = t_json["name"].get<std::string>();
				if (t_json.contains("description") && t_json["description"].is_string()) {
					tool.description = t_json["description"].get<std::string>();
				}
				if (t_json.contains("inputSchema") && t_json["inputSchema"].is_object()) {
					tool.schema_json = t_json["inputSchema"].dump();
				}
				bool is_enabled = config_manager::get_instance().is_mcp_tool_enabled(name_, tool.name, is_system_, true);
				tool.enabled = is_enabled;
				tools.push_back(tool);
			}
			tools_ = tools;
			event_logger::get_instance().log("MCP server '{}' initialized successfully. Discovered {} tools.", name_,
							 tools_.size());
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log("MCP server '{}' handshake exception: {}", name_, e.what());
		stop();
		return false;
	}

	return true;
}

void mcp_server::stop()
{
	std::lock_guard<std::mutex> lock(state_mutex_);
	if (pid_ <= 0) {
		return;
	}

	event_logger::get_instance().log("Stopping MCP server '{}' (PID {})...", name_, pid_);

	reader_running_ = false;

	if (stdin_fd_ >= 0) {
		close(stdin_fd_);
		stdin_fd_ = -1;
	}

	// Kill child process group
	kill(-pid_, SIGTERM);

	int status;
	int waited = 0;
	while (waited < 10) {
		pid_t res = waitpid(pid_, &status, WNOHANG);
		if (res == pid_ || res < 0) {
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		waited++;
	}

	if (waited >= 10) {
		kill(-pid_, SIGKILL);
		waitpid(pid_, &status, 0);
	}

	pid_ = 0;

	if (stdout_fd_ >= 0) {
		close(stdout_fd_);
		stdout_fd_ = -1;
	}
	if (stderr_fd_ >= 0) {
		close(stderr_fd_);
		stderr_fd_ = -1;
	}

	if (reader_thread_.joinable()) {
		reader_thread_.join();
	}
	if (stderr_thread_.joinable()) {
		stderr_thread_.join();
	}

	// Terminate pending requests
	{
		std::lock_guard<std::mutex> lock(requests_mutex_);
		for (auto &pair : pending_requests_) {
			nlohmann::json err = {{"id", pair.first}, {"error", {{"code", -32603}, {"message", "Server stopped."}}}};
			pair.second.set_value(err);
		}
		pending_requests_.clear();
	}

	event_logger::get_instance().log("MCP server '{}' stopped.", name_);
}

nlohmann::json mcp_server::send_request(const std::string &method, const nlohmann::json &params)
{
	if (!is_running()) {
		return {{"error", {{"code", -32603}, {"message", "Server not running."}}}};
	}

	int id;
	std::promise<nlohmann::json> prom;
	std::future<nlohmann::json> fut = prom.get_future();

	{
		std::lock_guard<std::mutex> lock(requests_mutex_);
		id = next_request_id_++;
		pending_requests_[id] = std::move(prom);
	}

	nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};

	std::string payload = req.dump() + "\n";
	ssize_t written = write(stdin_fd_, payload.c_str(), payload.length());
	if (written <= 0) {
		std::lock_guard<std::mutex> lock(requests_mutex_);
		pending_requests_.erase(id);
		return {{"error", {{"code", -32603}, {"message", "Write to server stdin failed."}}}};
	}

	// 10s execution timeout
	if (fut.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
		return fut.get();
	} else {
		std::lock_guard<std::mutex> lock(requests_mutex_);
		pending_requests_.erase(id);
		return {{"error", {{"code", -32603}, {"message", "Request timeout."}}}};
	}
}

void mcp_server::send_notification(const std::string &method, const nlohmann::json &params)
{
	if (!is_running()) {
		return;
	}

	nlohmann::json notif = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};

	std::string payload = notif.dump() + "\n";
	(void)write(stdin_fd_, payload.c_str(), payload.length());
}

void mcp_server::reader_loop()
{
	std::string buffer;
	char chunk[1024];
	while (reader_running_) {
		ssize_t bytes = read(stdout_fd_, chunk, sizeof(chunk) - 1);
		if (bytes <= 0) {
			break;
		}
		chunk[bytes] = '\0';
		buffer.append(chunk, bytes);

		size_t pos;
		while ((pos = buffer.find('\n')) != std::string::npos) {
			std::string line = buffer.substr(0, pos);
			buffer.erase(0, pos + 1);

			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
				line.pop_back();
			}

			if (line.empty()) {
				continue;
			}

			try {
				auto json = nlohmann::json::parse(line);
				if (json.contains("id")) {
					int id = -1;
					if (json["id"].is_number_integer()) {
						id = json["id"].get<int>();
					} else if (json["id"].is_string()) {
						id = std::stoi(json["id"].get<std::string>());
					}
					if (id != -1) {
						std::lock_guard<std::mutex> lock(requests_mutex_);
						auto it = pending_requests_.find(id);
						if (it != pending_requests_.end()) {
							it->second.set_value(json);
							pending_requests_.erase(it);
						}
					}
				}
			} catch (...) {
				// ignore
			}
		}
	}
}

void mcp_server::stderr_loop()
{
	std::string buffer;
	char chunk[1024];
	while (reader_running_) {
		ssize_t bytes = read(stderr_fd_, chunk, sizeof(chunk) - 1);
		if (bytes <= 0) {
			break;
		}
		chunk[bytes] = '\0';
		buffer.append(chunk, bytes);

		size_t pos;
		while ((pos = buffer.find('\n')) != std::string::npos) {
			std::string line = buffer.substr(0, pos);
			buffer.erase(0, pos + 1);

			while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
				line.pop_back();
			}

			if (!line.empty()) {
				event_logger::get_instance().log("[MCP Stderr: {}] {}", name_, line);
			}
		}
	}
}

std::unique_ptr<llm_tool> mcp_tool_validator::create_tool_impl(const nlohmann::json &args) const
{
	return std::make_unique<mcp_llm_tool>(server_name_, tool_.name, args);
}

std::string mcp_llm_tool::execute(tool_context &)
{
	auto server = mcp_manager::get_instance().find_server(server_name_);
	if (!server) {
		return "Error: MCP server '" + server_name_ + "' not found.";
	}
	if (!server->is_running()) {
		return "Error: MCP server '" + server_name_ + "' is not running.";
	}

	nlohmann::json params = {{"name", tool_name_}, {"arguments", args_}};

	try {
		auto res = server->send_request("tools/call", params);
		if (res.contains("error")) {
			return "MCP Error: " + res["error"].dump();
		}
		if (res.contains("result")) {
			auto result = res["result"];
			std::string output = "";
			if (result.contains("isError") && result["isError"].get<bool>()) {
				output += "Error from tool: ";
			}
			if (result.contains("content") && result["content"].is_array()) {
				for (const auto &item : result["content"]) {
					if (item.contains("type") && item["type"].get<std::string>() == "text") {
						output += item["text"].get<std::string>();
					} else {
						output += item.dump();
					}
				}
			} else {
				output += result.dump();
			}
			return output;
		}
		return "Error: Empty or invalid response from MCP server.";
	} catch (const std::exception &e) {
		return "Exception executing MCP tool: " + std::string(e.what());
	}
}

} // namespace agentlib
