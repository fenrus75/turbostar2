#pragma once
#include "llm_transport.h"
#include "ai_model.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace agentlib {

class replay_transport : public llm_transport {
public:
    replay_transport(const std::string& playback_file);
    
    transport_response post(const std::string& path, const std::string& json_body) override;
    bool post_stream(const std::string& path, const std::string& json_body, 
                     std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) override;
    std::string get_base_url() const override { return "replay://" + playback_file_; }
    std::string get_last_error() const override { return last_error_; }

    api_type detect_api_type() const {
        if (!log_array_.empty()) {
            std::string path = log_array_[0].value("path", "");
            if (path.find("responses") != std::string::npos) {
                return api_type::openai_response;
            }
            if (path.find("gemini") != std::string::npos) {
                return api_type::gemini;
            }
            if (path.find("copilot") != std::string::npos) {
                return api_type::copilot;
            }
        }
        return api_type::openai;
    }

private:
    std::string playback_file_;
    std::string last_error_;
    std::vector<nlohmann::json> log_array_;
    size_t current_index_ = 0;
};

} // namespace agentlib
