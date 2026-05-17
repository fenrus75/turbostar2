#pragma once
#include <string>
#include <memory>

namespace agentlib {

struct transport_response {
    int status_code;
    std::string body;
};

class llm_transport {
public:
    virtual ~llm_transport() = default;
    
    // Abstract POST request
    virtual transport_response post(const std::string& path, const std::string& json_body) = 0;
};

} // namespace agentlib
