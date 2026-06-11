#include "copilot_manager.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <vector>
#include <format>
#include <fstream>
#include "../fs_utils.h"
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

int copilot_manager::get_polling_interval() const
{
	std::lock_guard<std::mutex> lock(token_mutex_);
	return polling_interval_;
}

bool copilot_manager::start_device_flow(std::string& user_code, std::string& verification_uri)
{
	const char *env_client_id = std::getenv("GITHUB_CLIENT_ID");
	std::string client_id = (env_client_id && *env_client_id) ? env_client_id : "Ov23liad8MyaRSwsMRdB";

	event_logger::get_instance().log("Initiating GitHub OAuth Device Flow with client_id: {}", client_id);

	std::string escaped_client_id = escape_value(client_id);
	std::string post_fields = std::format("client_id={}&scope=read%3Auser", escaped_client_id);

	std::vector<std::string> headers = {
		"Accept: application/json"
	};

	auto res = perform_curl_request("https://github.com/login/device/code", "POST", headers, post_fields);
	event_logger::get_instance().log("GitHub Device Flow code response. Status: {}, Body: {}", res.status_code, res.body);
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
		polling_interval_ = j.value("interval", 5);
		last_poll_time_ = std::chrono::steady_clock::time_point{};
		event_logger::get_instance().log("GitHub Device Flow parsed. device_code: {}, user_code: {}, verification_uri: {}, interval: {}s", device_code_, user_code, verification_uri, polling_interval_);
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
		auto now = std::chrono::steady_clock::now();
		if (last_poll_time_ != std::chrono::steady_clock::time_point{}) {
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_poll_time_).count();
			if (elapsed < 2) {
				event_logger::get_instance().log("GitHub Access Token Poll throttled (elapsed={}s < 2s)", elapsed);
				return false;
			}
		}
		last_poll_time_ = now;
		dev_code = device_code_;
	}

	if (dev_code.empty()) {
		return false;
	}

	const char *env_client_id = std::getenv("GITHUB_CLIENT_ID");
	std::string client_id = (env_client_id && *env_client_id) ? env_client_id : "Ov23liad8MyaRSwsMRdB";

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
	
	std::string logged_body = res.body;
	size_t tok_pos = logged_body.find("access_token");
	if (tok_pos != std::string::npos) {
		size_t val_start = logged_body.find('"', tok_pos + 12);
		if (val_start != std::string::npos) {
			size_t val_end = logged_body.find('"', val_start + 1);
			if (val_end != std::string::npos && val_end > val_start) {
				logged_body.replace(val_start + 1, val_end - val_start - 1, "********");
			}
		} else {
			size_t eq_start = logged_body.find('=', tok_pos);
			if (eq_start != std::string::npos) {
				size_t amp_end = logged_body.find('&', eq_start);
				if (amp_end != std::string::npos && amp_end > eq_start) {
					logged_body.replace(eq_start + 1, amp_end - eq_start - 1, "********");
				} else {
					logged_body.replace(eq_start + 1, logged_body.length() - eq_start - 1, "********");
				}
			}
		}
	}
	
	event_logger::get_instance().log("GitHub Access Token Poll result: status_code={}, body={}", res.status_code, logged_body);
	
	if (res.status_code != 200) {
		return false;
	}

	try {
		auto j = json::parse(res.body);
		if (j.contains("access_token")) {
			std::string token = j["access_token"];
			set_github_access_token(token);
			event_logger::get_instance().log("GitHub OAuth authorization successful. Token length: {}", token.length());
			
			// Retrieve and format GitHub Models list
			query_and_write_github_models(token);
			
			return true;
		} else if (j.contains("error")) {
			std::string err = j["error"];
			if (err == "slow_down") {
				std::lock_guard<std::mutex> lock(token_mutex_);
				if (j.contains("interval") && j["interval"].is_number()) {
					polling_interval_ = j["interval"].get<int>();
				} else {
					polling_interval_ += 5;
				}
				event_logger::get_instance().log("GitHub Device Flow slow_down: increasing polling interval to {}s", polling_interval_);
			} else if (err != "authorization_pending") {
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

std::string copilot_manager::format_github_models_json(const std::string& catalog_json)
{
	try {
		auto catalog = json::parse(catalog_json);
		if (!catalog.is_array()) {
			return "[]";
		}

		json output_models = json::array();
		for (const auto &item : catalog) {
			if (!item.is_object()) {
				continue;
			}
			std::string id = item.value("id", "");
			if (id.empty()) {
				continue;
			}

			std::string name = item.value("name", id);
			std::string publisher = item.value("publisher", "");
			std::string summary = item.value("summary", "");
			std::string purpose;
			if (!publisher.empty()) {
				purpose = std::format("Publisher: {}. {}", publisher, summary);
			} else {
				purpose = summary;
			}

			int max_context_tokens = 131072;
			if (item.contains("limits") && item["limits"].is_object()) {
				max_context_tokens = item["limits"].value("max_input_tokens", 131072);
			}

			json model_entry;
			model_entry["id"] = id;
			model_entry["name"] = name;
			model_entry["url"] = "https://models.inference.ai.azure.com";
			model_entry["purpose"] = purpose;
			model_entry["cost_tx"] = 0.0;
			model_entry["cost_rx"] = 0.0;
			model_entry["api_key"] = "";
			model_entry["api_type"] = "copilot";
			model_entry["max_context_tokens"] = max_context_tokens;
			model_entry["cost_type"] = "paid_per_token";

			output_models.push_back(model_entry);
		}
		return output_models.dump(4);
	} catch (...) {
		return "[]";
	}
}

void copilot_manager::query_and_write_github_models(const std::string &/*token*/)
{
	event_logger::get_instance().log("query_and_write_github_models started.");

	std::string copilot_token = get_copilot_token();
	if (copilot_token.empty()) {
		event_logger::get_instance().log("Cannot fetch GitHub models catalog: Copilot token is empty");
		return;
	}

	std::vector<std::string> headers = {
		"Accept: application/json",
		"Authorization: Bearer " + copilot_token
	};

	auto res = perform_curl_request("https://api.githubcopilot.com/models", "GET", headers);
	event_logger::get_instance().log("GitHub models catalog fetch response. Status code: {}, Body size: {}", res.status_code, res.body.size());

	if (res.status_code != 200) {
		event_logger::get_instance().log("Failed to fetch GitHub models catalog. Status code: {}, response body: {}", res.status_code, res.body);
		return;
	}

	std::string formatted = format_github_models_json(res.body);
	if (formatted == "[]") {
		event_logger::get_instance().log("GitHub models catalog response could not be parsed or is empty.");
		return;
	}

	std::string cache_dir = fs_utils::get_global_cache_dir();
	std::string path = cache_dir + "/models.json.github";
	event_logger::get_instance().log("Writing GitHub models to path: {}", path);
	std::ofstream file(path);
	if (!file.is_open()) {
		event_logger::get_instance().log("Failed to open {} for writing GitHub models.", path);
		return;
	}

	file << formatted;
	event_logger::get_instance().log("Saved GitHub models to {}", path);
}

} // namespace agentlib
