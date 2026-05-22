#pragma once
#include <string>
#include <memory>
#include <functional>

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

    // Abstract streaming POST request
    virtual bool post_stream(const std::string& path, const std::string& json_body, 
                             std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) = 0;

    // Cancels an ongoing request (if supported by the transport)
    virtual void cancel() {}

    // Returns the base URL of this transport for error reporting
    virtual std::string get_base_url() const = 0;
};

} // namespace agentlib
