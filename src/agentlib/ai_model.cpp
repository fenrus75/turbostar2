#include "ai_model.h"

namespace agentlib {

double ai_model::calculate_and_record_cost(int tx_tokens, int rx_tokens) {
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

ai_model_registry& ai_model_registry::get_instance() {
    static ai_model_registry instance;
    return instance;
}

ai_model_registry::ai_model_registry() {
    // Standard baseline models
    register_model(std::make_shared<ai_model>(
        "local-default", "Local Host", "http://192.168.1.60:8080", "Default local LLM", 0.0, 0.0));
        
    register_model(std::make_shared<ai_model>(
        "gpt-4o", "GPT-4o", "https://api.openai.com/v1", "Complex coding and architecture", 5.00, 15.00));
        
    register_model(std::make_shared<ai_model>(
        "claude-3-5-sonnet", "Claude 3.5 Sonnet", "https://api.anthropic.com/v1", "Fast and cheap coding", 3.00, 15.00));
        
    register_model(std::make_shared<ai_model>(
        "gemini-1.5-pro", "Gemini 1.5 Pro", "https://generativelanguage.googleapis.com/v1beta/openai", "Huge context windows", 3.50, 10.50));
}

void ai_model_registry::register_model(std::shared_ptr<ai_model> model) {
    if (model) {
        models_[model->get_id()] = std::move(model);
    }
}

std::shared_ptr<ai_model> ai_model_registry::get_model(const std::string& id) const {
    auto it = models_.find(id);
    if (it != models_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<ai_model>> ai_model_registry::get_all_models() const {
    std::vector<std::shared_ptr<ai_model>> result;
    result.reserve(models_.size());
    for (const auto& [id, model] : models_) {
        result.push_back(model);
    }
    return result;
}

} // namespace agentlib