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

private:
    std::vector<nlohmann::json> log_array_;
    size_t current_index_ = 0;
};

} // namespace agentlib
