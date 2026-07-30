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

void a2a_server::setup_routes()
{
	// 1. GET /.well-known/agent.json -> Server directory / primary agent card
	server_->Get("/.well-known/agent.json", [this](const httplib::Request &, httplib::Response &res) {
		nlohmann::json root_card;
		root_card["protocol_version"] = "1.0";
		root_card["server"] = "Turbostar A2A Server";
		root_card["port"] = bound_port_.load();
		root_card["endpoints"] = {
			{"cards_index", "/a2a/v1/cards"},
			{"card_detail", "/a2a/v1/cards/{agent_name}"},
			{"task_create", "/a2a/v1/agents/{agent_name}/tasks"},
			{"task_status", "/a2a/v1/tasks/{task_id}"}
		};

		nlohmann::json agent_list = nlohmann::json::array();
		for (const auto &sa : agentlib::subagent_manager::get_instance().get_subagents()) {
			agent_list.push_back({
				{"name", sa.name},
				{"description", sa.description},
				{"read_only", sa.read_only},
				{"card_url", std::format("/a2a/v1/cards/{}", sa.name)}
			});
		}
		root_card["hosted_agents"] = agent_list;

		res.set_content(root_card.dump(2), "application/json");
	});

	// 2. GET /a2a/v1/cards -> List all registered agent cards
	server_->Get("/a2a/v1/cards", [](const httplib::Request &, httplib::Response &res) {
		nlohmann::json cards = nlohmann::json::array();
		for (const auto &sa : agentlib::subagent_manager::get_instance().get_subagents()) {
			std::string card_json = agentlib::subagent_manager::get_instance().get_a2a_card(sa.name);
			if (!card_json.empty()) {
				try {
					cards.push_back(nlohmann::json::parse(card_json));
				} catch (...) {
					cards.push_back({{"name", sa.name}, {"description", sa.description}});
				}
			}
		}
		res.set_content(cards.dump(2), "application/json");
	});

	// 3. GET /a2a/v1/cards/:name -> Get specific agent card
	server_->Get(R"(/a2a/v1/cards/([^/]+))", [](const httplib::Request &req, httplib::Response &res) {
		std::string agent_name = req.matches[1];
		std::string card_json = agentlib::subagent_manager::get_instance().get_a2a_card(agent_name);
		if (card_json.empty()) {
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Agent card not found for '{}'"}})", agent_name), "application/json");
		} else {
			res.set_content(card_json, "application/json");
		}
	});

	// 4. POST /a2a/v1/agents/:name/tasks -> Enqueue task for specific agent
	server_->Post(R"(/a2a/v1/agents/([^/]+)/tasks)", [this](const httplib::Request &req, httplib::Response &res) {
		std::string agent_name = req.matches[1];
		nlohmann::json input_params;
		if (!req.body.empty()) {
			try {
				input_params = nlohmann::json::parse(req.body);
			} catch (const std::exception &e) {
				res.status = 400;
				res.set_content(std::format(R"({{"error": "Invalid JSON body: {}"}})", e.what()), "application/json");
				return;
			}
		}

		std::string err;
		std::string task_id = create_task(agent_name, input_params, err);
		if (task_id.empty()) {
			res.status = 400;
			res.set_content(std::format(R"({{"error": "{}"}})", err), "application/json");
		} else {
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
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Task not found for id '{}'"}})", task_id), "application/json");
		} else {
			const auto &t = *task_opt;
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
		bool cancelled = cancel_task(task_id);
		if (!cancelled) {
			res.status = 404;
			res.set_content(std::format(R"({{"error": "Task not found or already completed: '{}'"}})", task_id), "application/json");
		} else {
			res.set_content(std::format(R"({{"status": "cancelled", "task_id": "{}"}})", task_id), "application/json");
		}
	});
}

std::string a2a_server::create_task(const std::string &agent_name, const nlohmann::json &input_params, std::string &out_error)
{
	auto sa = agentlib::subagent_manager::get_instance().find_subagent_by_name(agent_name);
	if (!sa) {
		out_error = std::format("Subagent '{}' is not registered.", agent_name);
		return "";
	}

	std::string task_id = generate_unique_task_id();
	std::string ts = get_iso_timestamp();

	a2a_task_info task;
	task.id = task_id;
	task.agent_name = agent_name;
	task.status = "completed"; // Immediately mark as completed for synchronous/synthetic tasks
	task.input_params = input_params;
	task.output_result = {
		{"status", "success"},
		{"summary", std::format("Task completed by {}", agent_name)},
		{"artifacts", nlohmann::json::array()}
	};
	task.created_at = ts;
	task.updated_at = ts;
	task.progress_percent = 100;

	{
		std::lock_guard<std::mutex> lock(tasks_mutex_);
		tasks_[task_id] = task;
	}

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
