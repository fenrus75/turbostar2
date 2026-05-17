#include "httplib_transport.h"
#include <httplib.h>

namespace agentlib {

httplib_transport::httplib_transport(const std::string& base_url) : base_url_(base_url) {}

transport_response httplib_transport::post(const std::string& path, const std::string& json_body) {
    httplib::Client cli(base_url_);
    auto res = cli.Post(path.c_str(), json_body, "application/json");

    transport_response response;
    if (res) {
        response.status_code = res->status;
        response.body = res->body;
    } else {
        response.status_code = -1;
        response.body = "Connection failed";
    }
    return response;
}

} // namespace agentlib
