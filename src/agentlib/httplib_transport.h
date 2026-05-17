#pragma once
#include "llm_transport.h"
#include <string>

namespace agentlib {

class httplib_transport : public llm_transport {
public:
    explicit httplib_transport(const std::string& base_url);
    
    transport_response post(const std::string& path, const std::string& json_body) override;

private:
    std::string base_url_;
};

} // namespace agentlib
