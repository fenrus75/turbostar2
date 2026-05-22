#pragma once
#include "llm_transport.h"
#include <string>
#include <memory>
#include <mutex>

namespace httplib {
    class Client;
}

namespace agentlib {

class httplib_transport : public llm_transport {
public:
    explicit httplib_transport(const std::string& base_url);
    ~httplib_transport() override;
    
    transport_response post(const std::string& path, const std::string& json_body) override;
    bool post_stream(const std::string& path, const std::string& json_body, 
                     std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) override;
    void cancel() override;
    std::string get_base_url() const override { return base_url_; }

private:
    std::string base_url_;
    std::unique_ptr<httplib::Client> cli_;
    std::mutex mutex_;
};

} // namespace agentlib
