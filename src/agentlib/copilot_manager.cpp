#include "copilot_manager.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "config_manager.h"
#include "event_logger.h"

using json = nlohmann::json;

namespace agentlib {

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
	httplib::Client cli("https://github.com");
	cli.set_connection_timeout(5);
	cli.set_read_timeout(10);
	cli.set_follow_location(true);

	httplib::Params params;
	params.emplace("client_id", "12345");
	params.emplace("scope", "read:user");

	httplib::Headers headers = {
		{"Accept", "application/json"}
	};

	event_logger::get_instance().log("Initiating GitHub OAuth Device Flow");

	auto res = cli.Post("/login/device/code", headers, params);
	if (!res) {
		event_logger::get_instance().log("GitHub Device Flow connection failed");
		return false;
	}

	if (res->status != 200) {
		event_logger::get_instance().log("GitHub Device Flow returned status {}", res->status);
		return false;
	}

	try {
		auto j = json::parse(res->body);
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

	httplib::Client cli("https://github.com");
	cli.set_connection_timeout(5);
	cli.set_read_timeout(10);
	cli.set_follow_location(true);

	httplib::Params params;
	params.emplace("client_id", "12345");
	params.emplace("device_code", dev_code);
	params.emplace("grant_type", "urn:ietf:params:oauth:grant-type:device_code");

	httplib::Headers headers = {
		{"Accept", "application/json"}
	};

	auto res = cli.Post("/login/oauth/access_token", headers, params);
	if (!res || res->status != 200) {
		return false;
	}

	try {
		auto j = json::parse(res->body);
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

	// Request new Copilot token from GitHub api
	httplib::Client cli("https://api.github.com");
	cli.set_connection_timeout(5);
	cli.set_read_timeout(10);
	cli.set_follow_location(true);

	httplib::Headers headers = {
		{"Authorization", "token " + access_token},
		{"User-Agent", "GithubCopilot/1.250.0"},
		{"Accept", "application/json"}
	};

	event_logger::get_instance().log("Requesting new short-lived Copilot token from GitHub API");

	auto res = cli.Get("/copilot_internal/v2/token", headers);
	if (!res) {
		event_logger::get_instance().log("Connection to api.github.com failed during Copilot token fetch");
		return "";
	}

	if (res->status != 200) {
		event_logger::get_instance().log("GitHub Copilot token API returned status {}", res->status);
		return "";
	}

	try {
		auto j = json::parse(res->body);
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
