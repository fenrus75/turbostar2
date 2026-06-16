#include "ai_model.h"
#include "model_server.h"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../config_manager.h"
#include "../event_logger.h"
#include "../fs_utils.h"

using json = nlohmann::json;

namespace agentlib
{

ai_model::ai_model(std::string id, std::string name, std::string url, std::string purpose, double cost_per_1m_tx, double cost_per_1m_rx,
		   std::string api_key, api_type type, int max_context_tokens, model_cost_type cost_type, std::string server_id)
    : id_(std::move(id)), name_(std::move(name)), purpose_(std::move(purpose)), cost_per_1m_tx_(cost_per_1m_tx),
      cost_per_1m_rx_(cost_per_1m_rx), max_context_tokens_(max_context_tokens), cost_type_(cost_type)
{
	if (server_id.empty()) {
		if (!url.empty()) {
			auto &reg = model_server_registry::get_instance();
			std::string derived_id = id_ + "_server";
			auto existing_srv = reg.get_server(derived_id);
			if (!existing_srv) {
				for (const auto &srv : reg.get_all_servers()) {
					if (srv->get_url() == url && srv->get_api_key() == api_key && srv->get_api_type() == type) {
						derived_id = srv->get_id();
						existing_srv = srv;
						break;
					}
				}
			}
			if (!existing_srv) {
				auto new_srv = std::make_shared<model_server>(derived_id, name_ + " Server", url, api_key, type);
				reg.register_server(new_srv);
				reg.save_servers();
			}
			server_id_ = derived_id;
		}
	} else {
		server_id_ = std::move(server_id);
		auto &reg = model_server_registry::get_instance();
		if (!reg.get_server(server_id_)) {
			auto new_srv = std::make_shared<model_server>(server_id_, name_ + " Server", url, api_key, type);
			reg.register_server(new_srv);
			reg.save_servers();
		}
	}

	if (server_id_.empty()) {
		std::string derived_id = id_ + "_server";
		auto &reg = model_server_registry::get_instance();
		auto existing_srv = reg.get_server(derived_id);
		if (!existing_srv) {
			auto new_srv = std::make_shared<model_server>(derived_id, name_ + " Server", "http://localhost", "", api_type::openai);
			reg.register_server(new_srv);
			reg.save_servers();
		}
		server_id_ = derived_id;
	}
}

std::string ai_model::get_url() const
{
	auto server = model_server_registry::get_instance().get_server(server_id_);
	if (server) {
		return server->get_url();
	}
	return "";
}

std::string ai_model::get_api_key() const
{
	auto server = model_server_registry::get_instance().get_server(server_id_);
	if (server) {
		return server->get_api_key();
	}
	return "";
}

api_type ai_model::get_api_type() const
{
	auto server = model_server_registry::get_instance().get_server(server_id_);
	if (server) {
		return server->get_api_type();
	}
	return api_type::openai;
}

double ai_model::calculate_and_record_cost(int tx_tokens, int rx_tokens)
{
	double cost = 0.0;
	cost += (tx_tokens / 1000000.0) * cost_per_1m_tx_;
	cost += (rx_tokens / 1000000.0) * cost_per_1m_rx_;

	global_tokens_tx_ += tx_tokens;
	global_tokens_rx_ += rx_tokens;

	// std::atomic<double> requires CAS loop or C++20 fetch_add (fetch_add for float/double is C++20)
	// Since we use C++23 we can just use +=
	global_cost_ += cost;

	return cost;
}

ai_model_registry &ai_model_registry::get_instance()
{
	static ai_model_registry instance;
	return instance;
}

ai_model_registry::ai_model_registry()
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/models.json";
	bool file_exists = std::filesystem::exists(path);

	load_models();
	if (!file_exists && models_.empty()) {
		// Standard baseline models
		register_model(std::make_shared<ai_model>("nvidia/MiniMax-M2.7-NVFP4", "Host", "http://192.168.1.55:8080",
							  "Default local LLM", 0.0, 0.0, "", api_type::openai));

		register_model(std::make_shared<ai_model>("gpt-4o", "GPT-4o", "https://api.openai.com/v1",
							  "Complex coding and architecture", 5.00, 15.00, "", api_type::openai));

		register_model(std::make_shared<ai_model>("claude-3-5-sonnet", "Claude 3.5 Sonnet", "https://api.anthropic.com/v1",
							  "Fast and cheap coding", 3.00, 15.00, "", api_type::openai));

		register_model(std::make_shared<ai_model>("gemini-1.5-pro", "Gemini 1.5 Pro", "https://generativelanguage.googleapis.com",
							  "Huge context windows", 3.50, 10.50, "", api_type::gemini));
		save_models();
	}
}

void ai_model_registry::register_model(std::shared_ptr<ai_model> model)
{
	if (model) {
		models_[model->get_id()] = std::move(model);
	}
}

void ai_model_registry::remove_model(const std::string &id)
{
	models_.erase(id);
}

void ai_model_registry::update_model(std::shared_ptr<ai_model> model)
{
	if (model) {
		models_[model->get_id()] = std::move(model);
	}
}

std::shared_ptr<ai_model> ai_model_registry::get_model(const std::string &id) const
{
	auto it = models_.find(id);
	if (it != models_.end()) {
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<ai_model> ai_model_registry::get_default_model() const
{
	std::string default_id = config_manager::get_instance().get_default_model_id();
	auto model = get_model(default_id);
	if (model) {
		return model;
	}

	// Fallback to the first available model if the configured one isn't found
	if (!models_.empty()) {
		return models_.begin()->second;
	}

	return nullptr;
}

std::vector<std::shared_ptr<ai_model>> ai_model_registry::get_all_models() const
{
	std::vector<std::shared_ptr<ai_model>> result;
	result.reserve(models_.size());
	for (const auto &[id, model] : models_) {
		result.push_back(model);
	}
	return result;
}

void ai_model_registry::load_models()
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/models.json";
	std::ifstream file(path);
	if (!file.is_open())
		return;

	try {
		json data;
		file >> data;
		if (data.is_array()) {
			for (const auto &item : data) {
				std::string id = item.value("id", "");
				std::string name = item.value("name", "");
				std::string url = item.value("url", "");
				std::string purpose = item.value("purpose", "");
				std::string api_key = item.value("api_key", "");
				double tx_cost = item.value("cost_tx", 0.0);
				double rx_cost = item.value("cost_rx", 0.0);
				std::string type_str = item.value("api_type", "openai");
				std::string server_id = item.value("server_id", "");
				api_type type = api_type::openai;
				if (type_str == "gemini") {
					type = api_type::gemini;
				} else if (type_str == "copilot") {
					type = api_type::copilot;
				} else if (type_str == "openai_response") {
					type = api_type::openai_response;
				}
				int max_tokens = item.value("max_context_tokens", 250000);
				std::string cost_type_str = item.value("cost_type", "paid_per_token");
				model_cost_type cost_type = model_cost_type::paid_per_token;
				if (cost_type_str == "free_local") {
					cost_type = model_cost_type::free_local;
				} else if (cost_type_str == "paid_per_request") {
					cost_type = model_cost_type::paid_per_request;
				}

				if (!id.empty()) {
					register_model(std::make_shared<ai_model>(id, name, url, purpose, tx_cost, rx_cost, api_key, type,
										  max_tokens, cost_type, server_id));
				}
			}
		}
		event_logger::get_instance().log("Loaded {} models from {}", models_.size(), path);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to load models from {}: {}", path, e.what());
	}
}

void ai_model_registry::save_models() const
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/models.json";
	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to open {} for writing models.", path);
		return;
	}

	json data = json::array();
	for (const auto &[id, model] : models_) {
		json item;
		item["id"] = model->get_id();
		item["name"] = model->get_name();
		item["server_id"] = model->get_server_id();
		item["purpose"] = model->get_purpose();
		item["cost_tx"] = model->get_cost_per_1m_tx();
		item["cost_rx"] = model->get_cost_per_1m_rx();
		item["max_context_tokens"] = model->get_max_context_tokens();

		std::string cost_type_str = "paid_per_token";
		if (model->get_cost_type() == model_cost_type::free_local)
			cost_type_str = "free_local";
		else if (model->get_cost_type() == model_cost_type::paid_per_request)
			cost_type_str = "paid_per_request";
		item["cost_type"] = cost_type_str;

		data.push_back(item);
	}

	file << data.dump(4);
	event_logger::get_instance().log("Saved {} models to {}", models_.size(), path);
}

} // namespace agentlib