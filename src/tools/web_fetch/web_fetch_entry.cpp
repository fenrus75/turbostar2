#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <curl/curl.h>
#include "../../agentlib/tool_context.h"
#include "../../agentlib/filter_registry.h"
#include "../../fs_utils.h"
#include "web_fetch.h"

namespace tools
{

static std::string extract_domain(const std::string &url)
{
	std::regex url_regex(R"(^https?://([^/:]+))");
	std::smatch match;
	if (std::regex_search(url, match, url_regex)) {
		return match[1].str();
	}
	return "";
}

static bool is_local_ip(const std::string &domain)
{
	if (domain == "localhost" || domain == "127.0.0.1" || domain == "::1")
		return true;
	if (domain.starts_with("192.168."))
		return true;
	if (domain.starts_with("10."))
		return true;
	if (domain.starts_with("172.")) {
		return true;
	}
	return false;
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	std::string *mem = static_cast<std::string *>(userp);
	mem->append(static_cast<const char *>(contents), realsize);
	return realsize;
}

static std::string perform_http_get(const std::string &url, int timeout_seconds = 30)
{
	CURL *curl = curl_easy_init();
	if (!curl) {
		return "Error: failed to initialize libcurl.";
	}

	std::string read_buffer;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &read_buffer);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

	CURLcode res = curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	if (res != CURLE_OK) {
		return "curl: (" + std::to_string(res) + ") " + curl_easy_strerror(res) + "\n\nProcess exited with code " + std::to_string(res) + "\n";
	}

	return read_buffer;
}

web_fetch_tool::web_fetch_tool(web_fetch_args args) : args_(std::move(args))
{
	domain_ = extract_domain(args_.url);
}

bool web_fetch_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string &out_error) const
{
	if (domain_.empty()) {
		out_error = "Could not extract domain from URL.";
		return false;
	}
	return true;
}

std::string web_fetch_tool::execute(agentlib::tool_context &ctx)
{
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
		if (args_.no_ask) {
			return "Error: Permission denied (silent failure).";
		}

		if (!ctx.queue) {
			return "Error: No event queue available to prompt the user for network permission.";
		}

		auto promise = std::make_shared<std::promise<std::string>>();
		auto future = promise->get_future();

		editor_event ev;
		ev.type = event_type::prompt_user;
		ev.payload = "Agent wants to fetch URL:\n" + args_.url + "\n\nAllow connection to " + domain_ + "?";

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
		} catch (const std::exception &e) {
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

	std::string output = perform_http_get(args_.url);

	if (!args_.filter.empty()) {
		bool success = false;
		std::string filtered = agentlib::filter_registry::get_instance().apply_filter(args_.filter, output, success);
		if (!success) {
			auto available = agentlib::filter_registry::get_instance().get_registered_filters();
			std::string avail_str;
			for (size_t i = 0; i < available.size(); ++i) {
				if (i > 0) avail_str += ", ";
				avail_str += "'" + available[i] + "'";
			}
			return "Error: Filter '" + args_.filter + "' is not registered. Available filters: " +
			       (avail_str.empty() ? "(none)" : avail_str);
		}
		output = filtered;
	}

	if (!args_.safe_output_path.empty()) {
		std::ofstream ofs(args_.safe_output_path, std::ios::binary);
		if (!ofs.is_open()) {
			return "Error: Failed to open output file " + args_.output_path + " for writing.";
		}
		ofs.write(output.data(), output.size());
		ofs.close();
		return std::format("Success: Downloaded {} bytes and saved to {}.", output.size(), args_.output_path);
	}

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