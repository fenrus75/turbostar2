#pragma once
#include "llm_transport.h"
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

private:
    std::string playback_file_;
    std::vector<nlohmann::json> log_array_;
    size_t current_index_ = 0;
};

} // namespace agentlib
