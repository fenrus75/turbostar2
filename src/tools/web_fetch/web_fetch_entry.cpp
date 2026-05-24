#include "web_fetch.h"
#include "../../fs_utils.h"
#include "../../agentlib/tool_context.h"
#include <fstream>
#include <filesystem>
#include <regex>
#include <future>

namespace tools {

static std::string extract_domain(const std::string& url) {
    std::regex url_regex(R"(^https?://([^/:]+))");
    std::smatch match;
    if (std::regex_search(url, match, url_regex)) {
        return match[1].str();
    }
    return "";
}

static bool is_local_ip(const std::string& domain) {
    if (domain == "localhost" || domain == "127.0.0.1" || domain == "::1") return true;
    if (domain.starts_with("192.168.")) return true;
    if (domain.starts_with("10.")) return true;
    if (domain.starts_with("172.")) {
        // Approximate 172.16.x.x - 172.31.x.x for simplicity, 
        // covering all 172.x is safer but overly broad. 
        // For security, just assume all 172. are local in this context or strict check
        return true;
    }
    return false;
}

web_fetch_tool::web_fetch_tool(std::string url) : url_(std::move(url)) {
    domain_ = extract_domain(url_);
}

bool web_fetch_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (domain_.empty()) {
        out_error = "Could not extract domain from URL.";
        return false;
    }
    return true;
}

std::string web_fetch_tool::execute(agentlib::tool_context& ctx) {
    std::string cache_dir = fs_utils::get_global_cache_dir();
    std::filesystem::path domains_file = std::filesystem::path(cache_dir) / "allowed_domains.txt";

    // Read existing rules
    char rule = '?'; // 'A' = always allow, 'D' = deny always
    std::ifstream in(domains_file);
    if (in) {
        std::string line;
        while (std::getline(in, line)) {
            if (line.length() > 2 && line[1] == ':') {
                if (line.substr(2) == domain_) {
                    rule = line[0];
                    break;
                }
            }
        }
    }

    if (rule == 'D') {
        return "Error: Permission denied to access domain: " + domain_ + " (Blacklisted)";
    }

    if (rule != 'A') {
        if (!ctx.queue) {
            return "Error: No event queue available to prompt the user for network permission.";
        }

        auto promise = std::make_shared<std::promise<std::string>>();
        auto future = promise->get_future();

        editor_event ev;
        ev.type = event_type::prompt_user;
        ev.payload = "Agent wants to fetch URL:\n" + url_ + "\n\nAllow connection to " + domain_ + "?";
        
        bool is_local = is_local_ip(domain_);
        if (is_local) {
            ev.prompt_options = {"Once", "Deny Always", "Deny"};
        } else {
            ev.prompt_options = {"Once", "Always", "Deny Always", "Deny"};
        }
        ev.prompt_promise = promise;

        ctx.queue->push(ev);

        std::string response;
        try {
            response = future.get();
        } catch (const std::exception& e) {
            return std::string("Error: Failed to get user response - ") + e.what();
        }

        if (response == "Deny") {
            return "Error: Permission denied by user for this request.";
        } else if (response == "Deny Always") {
            std::ofstream out(domains_file, std::ios::app);
            out << "D:" << domain_ << "\n";
            return "Error: Permission denied by user (Blacklisted).";
        } else if (response == "Always") {
            std::ofstream out(domains_file, std::ios::app);
            out << "A:" << domain_ << "\n";
        } else if (response != "Once") {
            return "Error: Unknown response from user.";
        }
    }

    // Permission granted (Once or Always)
    // Create config file for curl
    std::filesystem::path temp_dir = std::filesystem::path(fs_utils::get_project_tmp_dir());
    std::filesystem::path config_file = temp_dir / ("curl_config_" + std::to_string(std::hash<std::string>{}(url_)) + ".txt");
    
    std::ofstream out(config_file);
    if (!out) {
        return "Error: Failed to create temporary curl config file.";
    }
    out << "url = \"" << url_ << "\"\n";
    out.close();

    // Run curl: silent, follow redirects, max time 30s
    std::string cmd = "curl -sS -L -m 30 -K '" + config_file.string() + "'";
    std::string output = fs_utils::execute_command_sync(cmd);

    std::error_code ec;
    std::filesystem::remove(config_file, ec);

    if (output.empty()) {
        return "Success: But received empty response.";
    }

    if (output.length() > 20000) {
        output = output.substr(0, 20000);
        output += "\n\n...[output truncated due to length]...";
    }

    return "```\n" + output + "\n```";
}

} // namespace tools