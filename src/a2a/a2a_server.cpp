#include "a2a_server.h"
#include "agentlib/subagent_manager.h"
#include "event_logger.h"
#include <chrono>
#include <format>
#include <httplib.h>
#include <random>

namespace a2a
{

static std::string generate_unique_task_id()
{
	static std::mt19937_64 rng(std::random_device{}());
	std::uniform_int_distribution<uint64_t> dist;
	return std::format("task-{:016x}", dist(rng));
}

static std::string get_iso_timestamp()
{
	auto now = std::chrono::system_clock::now();
	return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

a2a_server &a2a_server::get_instance()
{
	static a2a_server instance;
	return instance;
}

a2a_server::a2a_server()
	: server_(std::make_unique<httplib::Server>())
{
}

a2a_server::~a2a_server()
{
	stop();
}

bool a2a_server::start(int base_port, int *out_bound_port)
{
	if (running_) {
		if (out_bound_port) *out_bound_port = bound_port_;
		return true;
	}

	setup_routes();

	int bound = 0;
	// Auto-bind fallback logic: try base_port up to base_port + 10
	for (int p = base_port; p <= base_port + 10; ++p) {
		if (server_->bind_to_port("0.0.0.0", p)) {
			bound = p;
			break;
		}
	}

	if (bound == 0) {
		event_logger::get_instance().log("a2a_server: Failed to bind to any port in range {}-{}", base_port, base_port + 10);
		return false;
	}

	bound_port_ = bound;
	running_ = true;
	if (out_bound_port) *out_bound_port = bound;

	server_thread_ = std::thread([this]() {
		event_logger::get_instance().log("a2a_server: Listening on port {}", bound_port_.load());
		server_->listen_after_bind();
		running_ = false;
	});

	return true;
}

void a2a_server::stop()
{
	if (running_) {
		server_->stop();
		if (server_thread_.joinable()) {
			server_thread_.join();
		}
		running_ = false;
		bound_port_ = 0;
	}
}

bool a2a_server::is_running() const
{
	return running_;
}

int a2a_server::get_bound_port() const
{
	return bound_port_;
}

void a2a_server::set_default_model(const std::string &model)
{
	std::lock_guard<std::mutex> lock(tasks_mutex_);
	default_model_ = model;
}

std::string a2a_server::get_default_model() const
{
	std::lock_guard<std::mutex> lock(tasks_mutex_);
	return default_model_;
}

void a2a_server::setup_routes()
{
	// Log incoming requests before routing
	server_->set_pre_routing_handler([](const httplib::Request &req, httplib::Response &) {
		event_logger::get_instance().log("a2a_server: Incoming {} request for '{}' from {}:{}",
			req.method, req.path, req.remote_addr, req.remote_port);
		return httplib::Server::HandlerResponse::Unhandled;
	});

	// Log completed responses after routing
	server_->set_logger([](const httplib::Request &req, const httplib::Response &res) {
		event_logger::get_instance().log("a2a_server: Completed {} '{}' -> HTTP {} (content-length: {})",
			req.method, req.path, res.status, res.body.size());
	});

	// Catch unhandled exceptions in route callbacks
	server_->set_exception_handler([](const httplib::Request &req, httplib::Response &res, std::exception_ptr ep) {
		std::string err_msg = "Unknown Exception";
		try {
			if (ep) std::rethrow_exception(ep);
		} catch (const std::exception &e) {
			err_msg = e.what();
		}
		event_logger::get_instance().log("a2a_server: Exception on {} '{}': {}", req.method, req.path, err_msg);
		res.status = 500;
		res.set_content(std::format(R"({{"error": "Internal Server Error", "detail": "{}"}})", err_msg), "application/json");
	});

	// 1. GET /.well-known/agent.json / agent-card.json / agent-card -> Server directory / primary agent card
	auto serve_well_known = [this](const httplib::Request &req, httplib::Response &res) {
		event_logger::get_instance().log("a2a_server: Serving {}", req.path);

		std::string host = req.get_header_value("Host");
		if (host.empty()) {
			host = std::format("localhost:{}", bound_port_.load());
		}
		std::string scheme = "http";
		std::string base_url = std::format("{}://{}", scheme, host);

		nlohmann::json root_card;
		root_card["name"] = "Turbostar A2A Server";
		root_card["description"] = "Turbostar A2A Server hosting multi-agent execution capabilities.";
		root_card["version"] = "1.0.0";
		root_card["url"] = base_url;
		root_card["protocol_version"] = "1.0";
		root_card["capabilities"] = {
			{"streaming", false},
			{"pushNotifications", false}
		};
		root_card["defaultInputModes"] = nlohmann::json::array({"text"});
		root_card["defaultOutputModes"] = nlohmann::json::array({"text"});

		nlohmann::json skills_array = nlohmann::json::array();
		nlohmann::json agent_list = nlohmann::json::array();

		for (const auto &sa : agentlib::subagent_manager::get_instance().get_a2a_subagents()) {
			nlohmann::json tags = nlohmann::json::array();
			tags.push_back(sa.name);
			if (sa.read_only) {
				tags.push_back("read-only");
			}
			for (const auto &fam : sa.tool_families) {
				std::string t = fam;
				if (t.starts_with(":plugin:")) t = t.substr(8);
				tags.push_back(t);
			}

			skills_array.push_back({
				{"id", sa.name},
				{"name", sa.name},
				{"description", sa.description.empty() ? (sa.name + " subagent capability") : sa.description},
				{"tags", tags}
			});
			agent_list.push_back({
				{"name", sa.name},
				{"description", sa.description},
				{"read_only", sa.read_only},
				{"card_url", std::format("/a2a/v1/cards/{}", sa.name)}
			});
		}

		if (skills_array.empty()) {
			skills_array.push_back({
				{"id", "general"},
				{"name", "general"},
				{"description", "General task execution capability"},
				{"tags", nlohmann::json::array({"general"})}
			});
		}

		root_card["skills"] = skills_array;
		root_card["endpoints"] = {
			{"cards_index", "/a2a/v1/cards"},
			{"card_detail", "/a2a/v1/cards/{agent_name}"},
			{"task_create", "/a2a/v1/agents/{agent_name}/tasks"},
			{"task_status", "/a2a/v1/tasks/{task_id}"}
		};
		root_card["hosted_agents"] = agent_list;
		root_card["port"] = bound_port_.load();
		root_card["server"] = "Turbostar A2A Server";

		res.status = 200;
		res.set_content(root_card.dump(2), "application/json");
	};

	server_->Get("/.well-known/agent.json", serve_well_known);
	server_->Get("/.well-known/agent-card.json", serve_well_known);
	server_->Get("/.well-known/agent-card", serve_well_known);

	// 2. GET /a2a/v1/cards -> List all registered exposed agent cards
	server_->Get("/a2a/v1/cards", [](const httplib::Request &req, httplib::Response &res) {
		if (req.path != "/a2a/v1/cards" && req.path != "/a2a/v1/cards/") {
			std::string agent_name = req.path;
			size_t last_slash = agent_name.find_last_of('/');
			if (last_slash != std::string::npos) {
				agent_name = agent_name.substr(last_slash + 1);
			}
			event_logger::get_instance().log("a2a_server: Querying card detail for '{}'", agent_name);
			std::string card_json = agentlib::subagent_manager::get_instance().get_a2a_card(agent_name);
			if (card_json.empty()) {
				event_logger::get_instance().log("a2a_server: Agent card for '{}' not found or unexposed (404)", agent_name);
				res.status = 404;
				res.set_content(std::format(R"({{"error": "Agent card not found for '{}'"}})", agent_name), "application/json");
			} else {
				res.status = 200;
				res.set_content(card_json, "application/json");
			}
			return;
		}
		event_logger::get_instance().log("a2a_server: Listing cards index");
		nlohmann::json cards = nlohmann::json::array();
		for (const auto &sa : agentlib::subagent_manager::get_instance().get_a2a_subagents()) {
			std::string card_json = agentlib::subagent_manager::get_instance().get_a2a_card(sa.name);
			if (!card_json.empty()) {
				try {
					cards.push_back(nlohmann::json::parse(card_json));
				} catch (...) {
					cards.push_back({{"name", sa.name}, {"description", sa.description}});
				}
			}
		}
		res.status = 200;
		res.set_content(cards.dump(2), "application/json");
	});

	// 3. GET /a2a/v1/cards/:name -> Get specific agent card
	server_->Get(R"(/a2a/v1/cards/(.+))", [](const httplib::Request &req, httplib::Response &res) {
		std::string agent_name = req.matches[1];
		event_logger::get_instance().log("a2a_server: Querying card detail for '{}'", agent_name);
		std::string card_json = agentlib::subagent_manager::get_instance().get_a2a_card(agent_name);
		if (card_json.empty()) {
			event_logger::get_instance().log("a2a_server: Agent card for '{}' not found or unexposed (404)", agent_name);
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Agent card not found for '{}'"}})", agent_name), "application/json");
		} else {
			res.status = 200;
			res.set_content(card_json, "application/json");
		}
	});

	// 4. POST /a2a/v1/agents/:name/tasks -> Enqueue task for specific agent
	server_->Post(R"(/a2a/v1/agents/([^/]+)/tasks)", [this](const httplib::Request &req, httplib::Response &res) {
		std::string agent_name = req.matches[1];
		event_logger::get_instance().log("a2a_server: Task creation request received for agent '{}'", agent_name);
		nlohmann::json input_params;
		if (!req.body.empty()) {
			try {
				input_params = nlohmann::json::parse(req.body);
			} catch (const std::exception &e) {
				event_logger::get_instance().log("a2a_server: Invalid JSON body in task creation: {}", e.what());
				res.status = 400;
				res.set_content(std::format(R"({{"error": "Invalid JSON body: {}"}})", e.what()), "application/json");
				return;
			}
		}

		std::string err;
		std::string task_id = create_task(agent_name, input_params, err);
		if (task_id.empty()) {
			event_logger::get_instance().log("a2a_server: Task creation failed for agent '{}': {}", agent_name, err);
			res.status = 400;
			res.set_content(std::format(R"({{"error": "{}"}})", err), "application/json");
		} else {
			event_logger::get_instance().log("a2a_server: Task '{}' enqueued successfully for agent '{}'", task_id, agent_name);
			res.status = 202; // Accepted
			nlohmann::json resp = {
				{"task_id", task_id},
				{"agent_name", agent_name},
				{"status", "enqueued"},
				{"status_url", std::format("/a2a/v1/tasks/{}", task_id)}
			};
			res.set_content(resp.dump(2), "application/json");
		}
	});

	// 5. GET /a2a/v1/tasks/:id -> Query task status
	server_->Get(R"(/a2a/v1/tasks/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
		std::string task_id = req.matches[1];
		auto task_opt = get_task(task_id);
		if (!task_opt) {
			event_logger::get_instance().log("a2a_server: Task query 404 for task_id '{}'", task_id);
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Task not found for id '{}'"}})", task_id), "application/json");
		} else {
			const auto &t = *task_opt;
			event_logger::get_instance().log("a2a_server: Task status query for '{}' -> status: '{}'", task_id, t.status);
			res.status = 200;
			nlohmann::json resp = {
				{"id", t.id},
				{"agent_name", t.agent_name},
				{"status", t.status},
				{"progress_percent", t.progress_percent},
				{"input_params", t.input_params},
				{"output_result", t.output_result},
				{"error_message", t.error_message},
				{"created_at", t.created_at},
				{"updated_at", t.updated_at}
			};
			res.set_content(resp.dump(2), "application/json");
		}
	});

	// 6. DELETE /a2a/v1/tasks/:id -> Cancel task
	server_->Delete(R"(/a2a/v1/tasks/([^/]+))", [this](const httplib::Request &req, httplib::Response &res) {
		std::string task_id = req.matches[1];
		event_logger::get_instance().log("a2a_server: Task cancel request for '{}'", task_id);
		bool cancelled = cancel_task(task_id);
		if (!cancelled) {
			event_logger::get_instance().log("a2a_server: Task cancel failed for '{}' (404/not running)", task_id);
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Task not found or already completed: '{}'"}})", task_id), "application/json");
		} else {
			event_logger::get_instance().log("a2a_server: Task '{}' cancelled", task_id);
			res.status = 200;
			res.set_content(std::format(R"({{"status": "cancelled", "task_id": "{}"}})", task_id), "application/json");
		}
	});
}

static std::string get_self_executable_path()
{
	const char *env_bin = getenv("TURBOSTAR_BIN_PATH");
	if (env_bin && *env_bin) {
		return std::string(env_bin);
	}
	try {
		std::string self = std::filesystem::read_symlink("/proc/self/exe").string();
		if (self.find("test_") != std::string::npos) {
			std::string parent = std::filesystem::path(self).parent_path().string();
			if (std::filesystem::exists(parent + "/turbostar")) {
				return parent + "/turbostar";
			}
			return "turbostar";
		}
		return self;
	} catch (...) {
		return "turbostar";
	}
}

std::string a2a_server::create_task(const std::string &agent_name, const nlohmann::json &input_params, std::string &out_error)
{
	auto sa = agentlib::subagent_manager::get_instance().find_subagent_by_name(agent_name);
	if (!sa || !sa->a2a_exposed) {
		out_error = std::format("Subagent '{}' is not registered or not exposed via A2A.", agent_name);
		return "";
	}

	std::string task_id = generate_unique_task_id();
	std::string ts = get_iso_timestamp();
	std::string task_dir = std::format("/tmp/turbostar_a2a_{}", task_id);
	try {
		std::filesystem::create_directories(task_dir);
	} catch (...) {}

	a2a_task_info task;
	task.id = task_id;
	task.agent_name = agent_name;
	task.status = "running";
	task.input_params = input_params;
	task.created_at = ts;
	task.updated_at = ts;
	task.progress_percent = 10;

	{
		std::lock_guard<std::mutex> lock(tasks_mutex_);
		tasks_[task_id] = task;
	}

	std::thread worker([this, task_id, agent_name, input_params, task_dir]() {
		std::string prompt;
		if (input_params.contains("instructions") && input_params["instructions"].is_string()) {
			prompt = input_params["instructions"].get<std::string>();
		} else if (input_params.contains("prompt") && input_params["prompt"].is_string()) {
			prompt = input_params["prompt"].get<std::string>();
		} else {
			prompt = input_params.dump();
		}

		std::string result_file = task_dir + "/result.json";
		std::string log_file = task_dir + "/session.log";
		std::string self_bin = get_self_executable_path();

		std::string model;
		if (input_params.contains("model") && input_params["model"].is_string()) {
			model = input_params["model"].get<std::string>();
		} else {
			model = get_default_model();
		}
		std::string model_flag = model.empty() ? "" : std::format(" --model '{}'", model);

		std::string cmd = std::format("'{}' --agent-name '{}' --prompt '{}' --project-dir '{}' --output-file '{}' --log '{}'{}"
			" --exit-immediately 0.5 >/dev/null 2>&1",
			self_bin, agent_name, prompt, task_dir, result_file, log_file, model_flag);

		int rc = ::system(cmd.c_str());

		std::lock_guard<std::mutex> lock(tasks_mutex_);
		auto it = tasks_.find(task_id);
		if (it != tasks_.end()) {
			it->second.updated_at = get_iso_timestamp();
			if (rc == 0 && std::filesystem::exists(result_file)) {
				try {
					std::ifstream f(result_file);
					nlohmann::json res_json = nlohmann::json::parse(f);
					it->second.output_result = res_json;
					it->second.status = "completed";
					it->second.progress_percent = 100;
				} catch (...) {
					it->second.status = "completed";
					it->second.output_result = {{"status", "success"}, {"task_dir", task_dir}};
					it->second.progress_percent = 100;
				}
			} else {
				it->second.status = "failed";
				it->second.error_message = std::format("Subprocess exited with code {}", rc);
				it->second.progress_percent = 100;
			}
		}
	});
	worker.detach();

	return task_id;
}

std::optional<a2a_task_info> a2a_server::get_task(const std::string &task_id) const
{
	std::lock_guard<std::mutex> lock(tasks_mutex_);
	auto it = tasks_.find(task_id);
	if (it != tasks_.end()) {
		return it->second;
	}
	return std::nullopt;
}

bool a2a_server::cancel_task(const std::string &task_id)
{
	std::lock_guard<std::mutex> lock(tasks_mutex_);
	auto it = tasks_.find(task_id);
	if (it != tasks_.end()) {
		if (it->second.status != "completed" && it->second.status != "failed") {
			it->second.status = "cancelled";
			it->second.updated_at = get_iso_timestamp();
			return true;
		}
	}
	return false;
}

} // namespace a2a
