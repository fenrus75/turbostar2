#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace agentlib
{

enum class api_type { openai, gemini, copilot, openai_response, claude };

enum class model_cost_type { free_local, paid_per_token, paid_per_request };
 
struct model_capabilities {
	bool vision{false};
	bool video{false};
	bool audio{false};
	bool coding{false};
};
 
class ai_model
{
      public:
	ai_model(std::string id, std::string name, std::string url, std::string purpose, double cost_per_1m_tx, double cost_per_1m_rx,
		 std::string api_key = "", api_type type = api_type::openai, int max_context_tokens = 250000,
		 model_cost_type cost_type = model_cost_type::paid_per_token, std::string server_id = "", bool from_download = false);

	std::string get_id() const
	{
		return id_;
	}
	std::string get_name() const
	{
		return name_;
	}
	std::string get_url() const;
	std::string get_purpose() const
	{
		return purpose_;
	}
	std::string get_api_key() const;
	api_type get_api_type() const;
	int get_max_context_tokens() const
	{
		return max_context_tokens_;
	}
	model_cost_type get_cost_type() const
	{
		return cost_type_;
	}

	double get_cost_per_1m_tx() const
	{
		return cost_per_1m_tx_;
	}
	double get_cost_per_1m_rx() const
	{
		return cost_per_1m_rx_;
	}

	void set_name(std::string name)
	{
		name_ = std::move(name);
	}
	void set_purpose(std::string purpose)
	{
		purpose_ = std::move(purpose);
	}
	void set_max_context_tokens(int max_tokens)
	{
		max_context_tokens_ = max_tokens;
	}
	void set_cost_type(model_cost_type cost_type)
	{
		cost_type_ = cost_type;
	}
	void set_cost_per_1m_tx(double cost)
	{
		cost_per_1m_tx_ = cost;
	}
	void set_cost_per_1m_rx(double cost)
	{
		cost_per_1m_rx_ = cost;
	}
	std::string get_server_id() const
	{
		return server_id_;
	}
	void set_server_id(std::string server_id)
	{
		server_id_ = std::move(server_id);
	}
	bool get_from_download() const
	{
		return from_download_;
	}
	void set_from_download(bool from_download)
	{
		from_download_ = from_download;
	}
	model_capabilities get_capabilities() const
	{
		return capabilities_;
	}
	void set_capabilities(model_capabilities capabilities)
	{
		capabilities_ = capabilities;
	}
 
	uint64_t get_creation_timestamp() const
	{
		return creation_timestamp_;
	}
	void set_creation_timestamp(uint64_t ts)
	{
		creation_timestamp_ = ts;
	}

	double calculate_score() const;

	int get_global_tokens_tx() const
	{
		return global_tokens_tx_;
	}
	int get_global_tokens_rx() const
	{
		return global_tokens_rx_;
	}
	double get_global_cost() const
	{
		return global_cost_;
	}
 
	// Calculates cost for this specific turn, and accumulates into the global tracker
	double calculate_and_record_cost(int tx_tokens, int rx_tokens);
 
      private:
	std::string id_;
	std::string name_;
	std::string purpose_;
	double cost_per_1m_tx_;
	double cost_per_1m_rx_;
	int max_context_tokens_;
	model_cost_type cost_type_;
	std::string server_id_;
	bool from_download_;
	model_capabilities capabilities_;
	uint64_t creation_timestamp_{0};

	std::atomic<int> global_tokens_tx_{0};
	std::atomic<int> global_tokens_rx_{0};
	std::atomic<double> global_cost_{0.0};
};

class ai_model_registry
{
      public:
	static ai_model_registry &get_instance();

	void register_model(std::shared_ptr<ai_model> model);
	std::shared_ptr<ai_model> get_model(const std::string &id) const;
	std::shared_ptr<ai_model> get_default_model() const;
	std::vector<std::shared_ptr<ai_model>> get_all_models() const;

	void remove_model(const std::string &id);
	void update_model(std::shared_ptr<ai_model> model);

	void load_models();
	void save_models() const;

      private:
	ai_model_registry();
	std::map<std::string, std::shared_ptr<ai_model>> models_;
};

} // namespace agentlib