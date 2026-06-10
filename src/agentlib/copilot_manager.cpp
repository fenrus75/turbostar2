#include "copilot_manager.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <vector>
#include <format>
#include "config_manager.h"
#include "event_logger.h"

using json = nlohmann::json;

namespace agentlib {

static size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t total_size = size * nmemb;
	std::string *str = static_cast<std::string *>(userp);
	str->append(static_cast<char *>(contents), total_size);
	return total_size;
}

struct http_response {
	int status_code = -1;
	std::string body;
};

static http_response perform_curl_request(
	const std::string &url,
	const std::string &method,
	const std::vector<std::string> &headers,
	const std::string &post_fields = ""
) {
	http_response resp;
	CURL *curl = curl_easy_init();
	if (!curl) {
		return resp;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	
	// Timeout settings
	const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
	if (in_testsuite && std::string(in_testsuite) == "1") {
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
	} else {
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
	}

	// Response buffer
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

	// Setup method
	if (method == "POST") {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		if (!post_fields.empty()) {
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields.c_str());
		}
	}

	// Setup headers
	struct curl_slist *header_list = nullptr;
	for (const auto &h : headers) {
		header_list = curl_slist_append(header_list, h.c_str());
	}
	if (header_list) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
	}

	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		resp.status_code = static_cast<int>(http_code);
	} else {
		event_logger::get_instance().log("curl error for URL {}: {}", url, curl_easy_strerror(res));
	}

	if (header_list) {
		curl_slist_free_all(header_list);
	}
	curl_easy_cleanup(curl);

	return resp;
}

static std::string escape_value(const std::string &val)
{
	CURL *curl = curl_easy_init();
	if (!curl) return val;
	char *escaped = curl_easy_escape(curl, val.c_str(), static_cast<int>(val.length()));
	std::string res = val;
	if (escaped) {
		res = escaped;
		curl_free(escaped);
	}
	curl_easy_cleanup(curl);
	return res;
}

copilot_manager::copilot_manager()
{
	github_access_token_ = config_manager::get_instance().get_github_access_token();
}

copilot_manager& copilot_manager::get_instance()
{
	static copilot_manager instance;
	return instance;
}

void copilot_manager::set_github_access_token(const std::string& token)
{
	std::lock_guard<std::mutex> lock(token_mutex_);
	github_access_token_ = token;
	config_manager::get_instance().set_github_access_token(token);
	config_manager::get_instance().save_global();
}

std::string copilot_manager::get_github_access_token() const
{
	std::lock_guard<std::mutex> lock(token_mutex_);
	return github_access_token_;
}

bool copilot_manager::is_authenticated() const
{
	std::lock_guard<std::mutex> lock(token_mutex_);
	return !github_access_token_.empty();
}

bool copilot_manager::start_device_flow(std::string& user_code, std::string& verification_uri)
{
	const char *env_client_id = std::getenv("GITHUB_CLIENT_ID");
	std::string client_id = (env_client_id && *env_client_id) ? env_client_id : "12345";

	event_logger::get_instance().log("Initiating GitHub OAuth Device Flow");

	std::string escaped_client_id = escape_value(client_id);
	std::string post_fields = std::format("client_id={}&scope=read%3Auser", escaped_client_id);

	std::vector<std::string> headers = {
		"Accept: application/json"
	};

	auto res = perform_curl_request("https://github.com/login/device/code", "POST", headers, post_fields);
	if (res.status_code != 200) {
		event_logger::get_instance().log("GitHub Device Flow connection failed (status code: {})", res.status_code);
		return false;
	}

	try {
		auto j = json::parse(res.body);
		std::lock_guard<std::mutex> lock(token_mutex_);
		device_code_ = j.value("device_code", "");
		user_code = j.value("user_code", "");
		verification_uri = j.value("verification_uri", "");
		return !device_code_.empty() && !user_code.empty();
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to parse GitHub Device Flow response: {}", e.what());
		return false;
	}
}

bool copilot_manager::poll_device_authorization(int /*interval_seconds*/)
{
	std::string dev_code;
	{
		std::lock_guard<std::mutex> lock(token_mutex_);
		dev_code = device_code_;
	}

	if (dev_code.empty()) {
		return false;
	}

	const char *env_client_id = std::getenv("GITHUB_CLIENT_ID");
	std::string client_id = (env_client_id && *env_client_id) ? env_client_id : "12345";

	std::string post_fields = std::format(
		"client_id={}&device_code={}&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code",
		escape_value(client_id),
		escape_value(dev_code)
	);

	const char *env_client_secret = std::getenv("GITHUB_CLIENT_SECRET");
	if (env_client_secret && *env_client_secret) {
		post_fields += std::format("&client_secret={}", escape_value(env_client_secret));
	}

	std::vector<std::string> headers = {
		"Accept: application/json"
	};

	auto res = perform_curl_request("https://github.com/login/oauth/access_token", "POST", headers, post_fields);
	if (res.status_code != 200) {
		return false;
	}

	try {
		auto j = json::parse(res.body);
		if (j.contains("access_token")) {
			std::string token = j["access_token"];
			set_github_access_token(token);
			event_logger::get_instance().log("GitHub OAuth authorization successful");
			return true;
		} else if (j.contains("error")) {
			std::string err = j["error"];
			if (err != "authorization_pending") {
				event_logger::get_instance().log("GitHub Device Flow error: {}", err);
			}
		}
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to parse GitHub token response: {}", e.what());
	}

	return false;
}

std::string copilot_manager::get_copilot_token()
{
	std::string access_token;
	std::string cached_token;
	std::chrono::system_clock::time_point exp;

	{
		std::lock_guard<std::mutex> lock(token_mutex_);
		access_token = github_access_token_;
		cached_token = cached_copilot_token_;
		exp = expires_at_;
	}

	if (access_token.empty()) {
		event_logger::get_instance().log("Cannot retrieve Copilot token: GitHub access token is empty");
		return "";
	}

	// Check if cached token is still valid (using a 5 minute safety buffer)
	auto now = std::chrono::system_clock::now();
	if (!cached_token.empty() && now + std::chrono::minutes(5) < exp) {
		return cached_token;
	}

	event_logger::get_instance().log("Requesting new short-lived Copilot token from GitHub API");

	std::vector<std::string> headers = {
		"Authorization: token " + access_token,
		"User-Agent: GithubCopilot/1.250.0",
		"Accept: application/json"
	};

	auto res = perform_curl_request("https://api.github.com/copilot_internal/v2/token", "GET", headers);
	if (res.status_code != 200) {
		event_logger::get_instance().log("GitHub Copilot token API returned status {}", res.status_code);
		return "";
	}

	try {
		auto j = json::parse(res.body);
		std::string new_token = j.value("token", "");
		long long expires_epoch = j.value("expires_at", 0LL);
		
		if (!new_token.empty()) {
			std::lock_guard<std::mutex> lock(token_mutex_);
			cached_copilot_token_ = new_token;
			expires_at_ = std::chrono::system_clock::from_time_t(static_cast<time_t>(expires_epoch));
			return cached_copilot_token_;
		}
	} catch (const std::exception& e) {
		event_logger::get_instance().log("Failed to parse Copilot token JSON response: {}", e.what());
	}

	return "";
}

} // namespace agentlib
