#include "model_server.h"
#include <algorithm>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include "../config_manager.h"
#include "../event_logger.h"
#include "../fs_utils.h"

using json = nlohmann::json;

namespace agentlib
{

model_server_registry &model_server_registry::get_instance()
{
	static model_server_registry instance;
	return instance;
}

model_server_registry::model_server_registry()
{
	load_servers();
}

void model_server_registry::register_server(std::shared_ptr<model_server> server)
{
	if (server) {
		servers_[server->get_id()] = server;
	}
}

std::shared_ptr<model_server> model_server_registry::get_server(const std::string &id) const
{
	if (id == "none" || id.empty()) {
		static auto none_srv = std::make_shared<model_server>("none", "None", "", "", api_type::openai);
		return none_srv;
	}
	auto it = servers_.find(id);
	if (it != servers_.end()) {
		return it->second;
	}
	return nullptr;
}

std::vector<std::shared_ptr<model_server>> model_server_registry::get_all_servers() const
{
	std::vector<std::shared_ptr<model_server>> result;
	result.reserve(servers_.size());
	for (const auto &[id, server] : servers_) {
		result.push_back(server);
	}
	return result;
}

void model_server_registry::remove_server(const std::string &id)
{
	servers_.erase(id);
}

void model_server_registry::update_server(std::shared_ptr<model_server> server)
{
	if (server) {
		servers_[server->get_id()] = server;
	}
}

void model_server_registry::load_servers()
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/servers.json";
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
				std::string api_key = item.value("api_key", "");
				std::string type_str = item.value("api_type", "openai");
				api_type type = api_type::openai;
				if (type_str == "gemini") {
					type = api_type::gemini;
				} else if (type_str == "copilot") {
					type = api_type::copilot;
				} else if (type_str == "openai_response") {
					type = api_type::openai_response;
				}

				if (!id.empty()) {
					register_server(std::make_shared<model_server>(id, name, url, api_key, type));
				}
			}
		}
		event_logger::get_instance().log("Loaded {} servers from {}", servers_.size(), path);
	} catch (const std::exception &e) {
		event_logger::get_instance().log("Failed to load servers from {}: {}", path, e.what());
	}
}

void model_server_registry::save_servers() const
{
	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/servers.json";
	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to open {} for writing servers.", path);
		return;
	}

	json data = json::array();
	for (const auto &[id, server] : servers_) {
		json item;
		item["id"] = server->get_id();
		item["name"] = server->get_name();
		item["url"] = server->get_url();
		item["api_key"] = server->get_api_key();

		std::string api_type_str = "openai";
		if (server->get_api_type() == api_type::gemini)
			api_type_str = "gemini";
		else if (server->get_api_type() == api_type::copilot)
			api_type_str = "copilot";
		else if (server->get_api_type() == api_type::openai_response)
			api_type_str = "openai_response";
		item["api_type"] = api_type_str;

		data.push_back(item);
	}

	file << data.dump(4);
	event_logger::get_instance().log("Saved {} servers to {}", servers_.size(), path);
}

} // namespace agentlib
