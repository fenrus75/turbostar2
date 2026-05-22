#include "replay_transport.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace agentlib {

replay_transport::replay_transport(const std::string& playback_file) : playback_file_(playback_file) {
    std::ifstream file(playback_file);
    if (file.is_open()) {
        try {
            json data;
            file >> data;
            if (data.is_array()) {
                for (const auto& item : data) {
                    log_array_.push_back(item);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[replay_transport] Error parsing playback file: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "[replay_transport] Warning: Could not open playback file " << playback_file << std::endl;
    }
}

transport_response replay_transport::post(const std::string& /*path*/, const std::string& /*json_body*/) {
    transport_response res;
    if (current_index_ < log_array_.size()) {
        const auto& interaction = log_array_[current_index_++];
        // We could verify that the path and request match, but for now simple sequential playback
        res.status_code = interaction["response"].value("status_code", 200);
        
        auto body_json = interaction["response"]["body"];
        if (body_json.is_string()) {
            res.body = body_json.get<std::string>();
        } else {
            res.body = body_json.dump();
        }
    } else {
        res.status_code = 404;
        res.body = "{\"error\": \"End of playback file reached\"}";
    }
    return res;
}

bool replay_transport::post_stream(const std::string& path, const std::string& json_body, 
                                    std::function<bool(const char* data, size_t len, size_t off, size_t total)> callback) {
    // For replay, we just deliver the whole body in one chunk if we find a match.
    auto res = post(path, json_body);
    if (res.status_code == 200) {
        callback(res.body.c_str(), res.body.length(), 0, res.body.length());
        return true;
    }
    return false;
}

} // namespace agentlib
