#include "get_current_datetime.h"
#include <sstream>
#include <chrono>
#include <ctime>

namespace tools {

bool get_current_datetime_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& /*out_error*/) const {
    return true;
}

std::string get_current_datetime_tool::execute(agentlib::tool_context& /*ctx*/) {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::tm* local_tm = std::localtime(&now_time_t);
    
    char tz_buf[64];
    std::strftime(tz_buf, sizeof(tz_buf), "%Z", local_tm);

    std::stringstream ss;
    ss << "| Metric | Value |\n";
    ss << "| ------ | ----- |\n";
    ss << "| Unix Time | " << now_time_t << " |\n";
    ss << "| Year | " << (local_tm->tm_year + 1900) << " |\n";
    ss << "| Month | " << (local_tm->tm_mon + 1) << " |\n";
    ss << "| Day | " << local_tm->tm_mday << " |\n";
    ss << "| Hour | " << local_tm->tm_hour << " |\n";
    ss << "| Minute | " << local_tm->tm_min << " |\n";
    ss << "| Second | " << local_tm->tm_sec << " |\n";
    ss << "| Timezone | " << tz_buf << " |\n";

    return ss.str();
}

} // namespace tools
