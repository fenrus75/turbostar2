#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include "ai_model.h"

namespace agentlib
{

class model_server
{
      public:
	model_server(std::string id, std::string name, std::string url, std::string api_key = "", api_type type = api_type::openai)
	    : id_(std::move(id)), name_(std::move(name)), url_(std::move(url)), api_key_(std::move(api_key)), type_(type)
	{
	}

	std::string get_id() const { return id_; }
	std::string get_name() const { return name_; }
	std::string get_url() const { return url_; }
	std::string get_api_key() const { return api_key_; }
	api_type get_api_type() const { return type_; }

	void set_name(std::string name) { name_ = std::move(name); }
	void set_url(std::string url) { url_ = std::move(url); }
	void set_api_key(std::string key) { api_key_ = std::move(key); }
	void set_api_type(api_type type) { type_ = type; }

      private:
	std::string id_;
	std::string name_;
	std::string url_;
	std::string api_key_;
	api_type type_;
};

class model_server_registry
{
      public:
	static model_server_registry &get_instance();

	void register_server(std::shared_ptr<model_server> server);
	std::shared_ptr<model_server> get_server(const std::string &id) const;
	std::vector<std::shared_ptr<model_server>> get_all_servers() const;

	void remove_server(const std::string &id);
	void update_server(std::shared_ptr<model_server> server);

	void load_servers();
	void save_servers() const;

      private:
	model_server_registry();
	std::map<std::string, std::shared_ptr<model_server>> servers_;
};

} // namespace agentlib
