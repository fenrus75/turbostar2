#pragma once
#include "llm_transport.h"
#include <string>
#include <memory>

namespace agentlib {

class recording_transport : public llm_transport {
public:
    recording_transport(std::shared_ptr<llm_transport> inner, const std::string& log_file);
    
    transport_response post(const std::string& path, const std::string& json_body) override;
    std::string get_base_url() const override { return inner_->get_base_url(); }

private:
    void append_to_log(const std::string& path, const std::string& request_body, const transport_response& res);

    std::shared_ptr<llm_transport> inner_;
    std::string log_file_;
};

} // namespace agentlib
