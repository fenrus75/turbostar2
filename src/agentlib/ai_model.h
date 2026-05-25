#pragma once

#include <string>
#include <atomic>
#include <memory>
#include <map>
#include <vector>

namespace agentlib {

enum class api_type {
    openai,
    gemini
};

class ai_model {
public:
    ai_model(std::string id, std::string name, std::string url, std::string purpose, double cost_per_1m_tx, double cost_per_1m_rx, std::string api_key = "", api_type type = api_type::openai, int max_context_tokens = 250000)
        : id_(std::move(id)), name_(std::move(name)), url_(std::move(url)), purpose_(std::move(purpose)), 
          cost_per_1m_tx_(cost_per_1m_tx), cost_per_1m_rx_(cost_per_1m_rx), api_key_(std::move(api_key)), type_(type), max_context_tokens_(max_context_tokens) {}

    std::string get_id() const { return id_; }
    std::string get_name() const { return name_; }
    std::string get_url() const { return url_; }
    std::string get_purpose() const { return purpose_; }
    std::string get_api_key() const { return api_key_; }
    api_type get_api_type() const { return type_; }
    int get_max_context_tokens() const { return max_context_tokens_; }

    double get_cost_per_1m_tx() const { return cost_per_1m_tx_; }
    double get_cost_per_1m_rx() const { return cost_per_1m_rx_; }

    void set_name(std::string name) { name_ = std::move(name); }
    void set_url(std::string url) { url_ = std::move(url); }
    void set_purpose(std::string purpose) { purpose_ = std::move(purpose); }
    void set_api_key(std::string key) { api_key_ = std::move(key); }
    void set_api_type(api_type type) { type_ = type; }
    void set_max_context_tokens(int max_tokens) { max_context_tokens_ = max_tokens; }
    void set_cost_per_1m_tx(double cost) { cost_per_1m_tx_ = cost; }
    void set_cost_per_1m_rx(double cost) { cost_per_1m_rx_ = cost; }

    int get_global_tokens_tx() const { return global_tokens_tx_; }
    int get_global_tokens_rx() const { return global_tokens_rx_; }
    double get_global_cost() const { return global_cost_; }

    // Calculates cost for this specific turn, and accumulates into the global tracker
    double calculate_and_record_cost(int tx_tokens, int rx_tokens);

private:
    std::string id_;
    std::string name_;
    std::string url_;
    std::string purpose_;
    double cost_per_1m_tx_;
    double cost_per_1m_rx_;
    std::string api_key_;
    api_type type_;
    int max_context_tokens_;

    std::atomic<int> global_tokens_tx_{0};
    std::atomic<int> global_tokens_rx_{0};
    std::atomic<double> global_cost_{0.0};
};

class ai_model_registry {
public:
    static ai_model_registry& get_instance();

    void register_model(std::shared_ptr<ai_model> model);
    std::shared_ptr<ai_model> get_model(const std::string& id) const;
    std::vector<std::shared_ptr<ai_model>> get_all_models() const;

    void remove_model(const std::string& id);
    void update_model(std::shared_ptr<ai_model> model);

    void load_models();
    void save_models() const;

private:
    ai_model_registry();
    std::map<std::string, std::shared_ptr<ai_model>> models_;
};

} // namespace agentlib