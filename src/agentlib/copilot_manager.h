#pragma once
#include <string>
#include <mutex>
#include <chrono>

namespace agentlib {

class copilot_manager {
public:
    static copilot_manager& get_instance();

    // Setters/getters for OAuth access token
    void set_github_access_token(const std::string& token);
    std::string get_github_access_token() const;

    // Device Flow Trigger
    bool start_device_flow(std::string& user_code, std::string& verification_uri);
    bool poll_device_authorization(int interval_seconds);

    // Copilot short-lived token retrieval
    std::string get_copilot_token();

    // Check if Copilot authentication is configured
    bool is_authenticated() const;

    // Transform raw catalog JSON into Turbostar's JSON representation
    static std::string format_github_models_json(const std::string& catalog_json);

private:
    copilot_manager();
    
    void query_and_write_github_models(const std::string &token);
    
    std::string github_access_token_;
    std::string cached_copilot_token_;
    std::chrono::system_clock::time_point expires_at_{std::chrono::system_clock::time_point::min()};
    std::string device_code_;
    
    /*
     * token_mutex_ protects the shared token lifecycle data:
     * - github_access_token_
     * - cached_copilot_token_
     * - expires_at_
     * - device_code_
     * 
     * Lock ordering guidelines:
     * - Acquired to read/write tokens or authorization state.
     * - Released immediately before any blocking HTTP calls (polling or token retrieval)
     *   to allow other threads to query active tokens or status concurrent with network operations.
     */
    mutable std::mutex token_mutex_;
};

} // namespace agentlib
