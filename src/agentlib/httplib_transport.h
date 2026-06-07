#pragma once
#include "llm_transport.h"
#include "ai_model.h"
#include <string>
#include <memory>
#include <mutex>
#include <vector>

namespace httplib {
    class Client;
}

namespace agentlib {

class httplib_transport : public llm_transport {
public:
    explicit httplib_transport(const std::string& base_url, const std::string& api_key = "");
    ~httplib_transport() override;
    
    transport_response post(const std::string& path, const std::string& json_body) override;
    bool post_stream(const std::string& path, const std::string& json_body, 
                     std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) override;
    void cancel() override;
    std::string get_base_url() const override { return base_url_; }
    std::string get_last_error() const override { return last_error_; }

private:
    std::string base_url_;
    std::string path_prefix_;
    std::string api_key_;
    std::string last_error_;
    std::unique_ptr<httplib::Client> cli_;
    /*
     * mutex_ serializes HTTP request executions on the underlying cli_ client.
     * Locking Rules:
     * - Held during post() and post_stream() for the entire duration of the request.
     * - Intentionally NOT held in cancel() because httplib::Client::stop() is thread-safe
     *   and is called concurrently to interrupt blocking network calls.
     */
    std::mutex mutex_;
};

std::vector<std::shared_ptr<ai_model>> fetch_openai_models(const std::string &server_url, std::string &error_out);

} // namespace agentlib

