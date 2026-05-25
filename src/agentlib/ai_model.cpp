#include "ai_model.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include "../fs_utils.h"
#include "../event_logger.h"

using json = nlohmann::json;

namespace agentlib
{

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
	load_models();
	if (models_.empty()) {
		// Standard baseline models
		register_model(
		    std::make_shared<ai_model>("local-default", "Local Host", "http://192.168.1.55:8080", "Default local LLM", 0.0, 0.0, "", api_type::openai));

		register_model(
		    std::make_shared<ai_model>("gpt-4o", "GPT-4o", "https://api.openai.com/v1", "Complex coding and architecture", 5.00, 15.00, "", api_type::openai));

		register_model(std::make_shared<ai_model>("claude-3-5-sonnet", "Claude 3.5 Sonnet", "https://api.anthropic.com/v1",
							  "Fast and cheap coding", 3.00, 15.00, "", api_type::openai));

		register_model(std::make_shared<ai_model>("gemini-1.5-pro", "Gemini 1.5 Pro",
							  "https://generativelanguage.googleapis.com", "Huge context windows", 3.50,
							  10.50, "", api_type::gemini));
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
				api_type type = api_type::openai;
				if (type_str == "gemini") {
					type = api_type::gemini;
				}
				int max_tokens = item.value("max_context_tokens", 250000);

				if (!id.empty()) {
					register_model(std::make_shared<ai_model>(id, name, url, purpose, tx_cost, rx_cost, api_key, type, max_tokens));
				}
			}
		}
		event_logger::get_instance().log("Loaded " + std::to_string(models_.size()) + " models from " + path);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to load models from " + path + ": " + e.what());
	}
}

void ai_model_registry::save_models() const
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/models.json";
	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to open " + path + " for writing models.");
		return;
	}

	json data = json::array();
	for (const auto &[id, model] : models_) {
		json item;
		item["id"] = model->get_id();
		item["name"] = model->get_name();
		item["url"] = model->get_url();
		item["purpose"] = model->get_purpose();
		item["api_key"] = model->get_api_key();
		item["cost_tx"] = model->get_cost_per_1m_tx();
		item["cost_rx"] = model->get_cost_per_1m_rx();
		item["api_type"] = (model->get_api_type() == api_type::gemini) ? "gemini" : "openai";
		item["max_context_tokens"] = model->get_max_context_tokens();
		data.push_back(item);
	}

	file << data.dump(4);
	event_logger::get_instance().log("Saved " + std::to_string(models_.size()) + " models to " + path);
}

} // namespace agentlib
