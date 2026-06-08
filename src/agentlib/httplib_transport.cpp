#include "httplib_transport.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <httplib.h>
#include <nlohmann/json.hpp>

namespace agentlib
{

static std::string format_rich_diagnostics(httplib::Error err, int err_num)
{
	(void)err;
	std::string diag;
	if (err_num != 0) {
		diag += std::format(", errno={} ({})", err_num, std::strerror(err_num));
	}
	const char *env_https_proxy = std::getenv("https_proxy");
	const char *env_http_proxy = std::getenv("http_proxy");
	if (env_https_proxy) {
		diag += std::format(", https_proxy={}", env_https_proxy);
	} else if (env_http_proxy) {
		diag += std::format(", http_proxy={}", env_http_proxy);
	}

	const char *ssl_cert_file = std::getenv("SSL_CERT_FILE");
	const char *ssl_cert_dir = std::getenv("SSL_CERT_DIR");
	if (ssl_cert_file) {
		diag += std::format(", SSL_CERT_FILE={}", ssl_cert_file);
	}
	if (ssl_cert_dir) {
		diag += std::format(", SSL_CERT_DIR={}", ssl_cert_dir);
	}
	return diag;
}

static std::string error_to_string(httplib::Error err)
{
	switch (err) {
		case httplib::Error::Success:
			return "Success";
		case httplib::Error::Unknown:
			return "Unknown error";
		case httplib::Error::Connection:
			return "Connection failed";
		case httplib::Error::BindIPAddress:
			return "Bind IP address failed";
		case httplib::Error::Read:
			return "Read failed";
		case httplib::Error::Write:
			return "Write failed";
		case httplib::Error::ExceedRedirectCount:
			return "Exceed redirect count";
		case httplib::Error::Canceled:
			return "Canceled";
		case httplib::Error::SSLConnection:
			return "SSL connection failed";
		case httplib::Error::SSLLoadingCerts:
			return "SSL loading certs failed";
		case httplib::Error::SSLServerVerification:
			return "SSL server verification failed";
		case httplib::Error::UnsupportedMultipartBoundaryChars:
			return "Unsupported multipart boundary characters";
		case httplib::Error::Compression:
			return "Compression failed";
		default:
			return "Internal httplib error code: " + std::to_string(static_cast<int>(err));
	}
}

httplib_transport::httplib_transport(const std::string &base_url, const std::string &api_key) : base_url_(base_url), api_key_(api_key)
{
	std::string host = base_url_;
	size_t scheme_end = host.find("://");
	size_t path_start = std::string::npos;

	if (scheme_end != std::string::npos) {
		path_start = host.find('/', scheme_end + 3);
	} else {
		path_start = host.find('/');
	}

	if (path_start != std::string::npos) {
		path_prefix_ = host.substr(path_start);
		host = host.substr(0, path_start);
		// Remove trailing slash from prefix if it exists to avoid double slashes later
		if (path_prefix_.length() > 1 && path_prefix_.back() == '/') {
			path_prefix_.pop_back();
		}
	}

	cli_ = std::make_unique<httplib::Client>(host);
	const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
	if (in_testsuite && std::string(in_testsuite) == "1") {
		cli_->set_connection_timeout(std::chrono::milliseconds(5000));
		cli_->set_read_timeout(std::chrono::milliseconds(15000));
	} else {
		cli_->set_connection_timeout(std::chrono::seconds(5));
		cli_->set_read_timeout(std::chrono::seconds(300));
	}
	cli_->set_follow_location(true);

	// Optional proxy support via environment variables
	const char *env_proxy = std::getenv("https_proxy");
	if (!env_proxy)
		env_proxy = std::getenv("http_proxy");

	if (env_proxy) {
		std::string proxy(env_proxy);
		size_t scheme_pos = proxy.find("://");
		if (scheme_pos != std::string::npos) {
			proxy = proxy.substr(scheme_pos + 3);
		}

		size_t port_pos = proxy.find(':');
		std::string p_host = proxy;
		int p_port = 80;

		if (port_pos != std::string::npos) {
			p_host = proxy.substr(0, port_pos);
			try {
				p_port = std::stoi(proxy.substr(port_pos + 1));
			} catch (...) {
			}
		}

		if (!p_host.empty() && p_host.back() == '/') {
			p_host.pop_back();
		}

		cli_->set_proxy(p_host, p_port);
	}
}
httplib_transport::~httplib_transport() = default;

void httplib_transport::cancel()
{
	// Note: We intentionally do not lock mutex_ here. post() and post_stream()
	// hold mutex_ for the entire duration of their network requests.
	// httplib::Client::stop() is thread-safe and designed to be called concurrently
	// to interrupt blocking network calls.
	if (cli_) {
		cli_->stop();
	}
}

transport_response httplib_transport::post(const std::string &path, const std::string &json_body)
{
	httplib::Result res;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!cli_)
			return {-1, "Client destroyed"};

		httplib::Headers headers;
		if (!api_key_.empty()) {
			if (base_url_.find("googleapis.com") != std::string::npos) {
				headers.emplace("x-goog-api-key", api_key_);
			} else {
				headers.emplace("Authorization", "Bearer " + api_key_);
			}
		}

		std::string full_path = path;
		if (!path_prefix_.empty()) {
			if (path.starts_with(path_prefix_)) {
				full_path = path;
			} else {
				full_path = path_prefix_ + path;
			}
		}
		res = cli_->Post(full_path.c_str(), headers, json_body, "application/json");
	}

	transport_response response;
	if (res) {
		response.status_code = res->status;
		response.body = res->body;
		last_error_ = "";
	} else {
		int err_num = errno;
		response.status_code = -1;
		last_error_ = error_to_string(res.error()) + format_rich_diagnostics(res.error(), err_num);
		response.body = "Connection failed or cancelled: " + last_error_;
	}
	return response;
}

bool httplib_transport::post_stream(const std::string &path, const std::string &json_body,
				    std::function<bool(const char *data, size_t len, size_t off, size_t total)> callback)
{
	httplib::Result res;
	std::string error_body;
	std::string requested_path;
	int status_code = 0;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!cli_)
			return false;

		httplib::Request req;
		req.method = "POST";

		std::string full_path = path;
		if (!path_prefix_.empty()) {
			if (path.starts_with(path_prefix_)) {
				full_path = path;
			} else {
				full_path = path_prefix_ + path;
			}
		}
		requested_path = full_path;
		req.path = full_path;

		req.body = json_body;
		req.set_header("Content-Type", "application/json");
		if (!api_key_.empty()) {
			if (base_url_.find("googleapis.com") != std::string::npos) {
				req.set_header("x-goog-api-key", api_key_);
			} else {
				req.set_header("Authorization", "Bearer " + api_key_);
			}
		}

		req.response_handler = [&](const httplib::Response &response) {
			status_code = response.status;
			return true;
		};

		req.content_receiver = [&](const char *data, size_t len, size_t off, size_t total) {
			if (status_code != 200) {
				error_body.append(data, len);
				return true;
			}
			return callback(data, len, off, total);
		};

		res = cli_->send(req);
	}

	if (!res) {
		int err_num = errno;
		last_error_ =
		    error_to_string(res.error()) + " (Path: " + requested_path + ")" + format_rich_diagnostics(res.error(), err_num);
		return false;
	}

	if (res->status != 200) {
		last_error_ = "HTTP " + std::to_string(res->status) + " [Path: " + requested_path + "]";
		if (!error_body.empty()) {
			last_error_ += "\nBody: " + error_body;
		} else if (!res->body.empty()) {
			last_error_ += "\nBody: " + res->body;
		}
		return false;
	}
	last_error_ = "";
	return true;
}

std::vector<std::shared_ptr<ai_model>> fetch_openai_models(const std::string &server_url, std::string &error_out)
{
	std::vector<std::shared_ptr<ai_model>> result;
	if (server_url.empty()) {
		error_out = "Server URL is empty";
		return result;
	}

	std::string target_url = server_url;
	if (target_url.back() == '/') {
		target_url.pop_back();
	}
	if (!target_url.ends_with("/models")) {
		target_url += "/models";
	}

	std::string host = target_url;
	size_t scheme_end = host.find("://");
	size_t path_start = std::string::npos;
	if (scheme_end != std::string::npos) {
		path_start = host.find('/', scheme_end + 3);
	} else {
		path_start = host.find('/');
	}

	std::string path = "/models";
	if (path_start != std::string::npos) {
		path = host.substr(path_start);
		host = host.substr(0, path_start);
	}

	try {
		httplib::Client cli(host);
		const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
		if (in_testsuite && std::string(in_testsuite) == "1") {
			cli.set_connection_timeout(std::chrono::milliseconds(5000));
			cli.set_read_timeout(std::chrono::milliseconds(15000));
		} else {
			cli.set_connection_timeout(std::chrono::seconds(5));
			cli.set_read_timeout(std::chrono::seconds(10));
		}
		cli.set_follow_location(true);

		// Proxy support identical to httplib_transport constructor
		const char *env_proxy = std::getenv("https_proxy");
		if (!env_proxy)
			env_proxy = std::getenv("http_proxy");
		if (env_proxy) {
			std::string proxy(env_proxy);
			size_t scheme_pos = proxy.find("://");
			if (scheme_pos != std::string::npos) {
				proxy = proxy.substr(scheme_pos + 3);
			}
			size_t port_pos = proxy.find(':');
			std::string p_host = proxy;
			int p_port = 80;
			if (port_pos != std::string::npos) {
				p_host = proxy.substr(0, port_pos);
				try {
					p_port = std::stoi(proxy.substr(port_pos + 1));
				} catch (...) {
				}
			}
			if (!p_host.empty() && p_host.back() == '/') {
				p_host.pop_back();
			}
			cli.set_proxy(p_host, p_port);
		}

		auto res = cli.Get(path.c_str());
		if (!res) {
			error_out = std::format("Failed to connect to {}: {}", host, error_to_string(res.error()));
			return result;
		}

		if (res->status != 200) {
			error_out = std::format("HTTP error code {}: {}", res->status, res->body.substr(0, 200));
			return result;
		}

		auto root = nlohmann::json::parse(res->body);
		if (!root.contains("data") || !root["data"].is_array()) {
			error_out = "Invalid response format: 'data' array not found";
			return result;
		}

		for (const auto &item : root["data"]) {
			if (item.contains("id") && item["id"].is_string()) {
				std::string id = item["id"];
				std::string name = item.value("name", id);
				std::string purpose = std::format("Imported from {}", server_url);

				result.push_back(std::make_shared<ai_model>(id, name, server_url, purpose, 0.0, 0.0, "", api_type::openai,
									    250000, model_cost_type::free_local));
			}
		}

		if (result.empty()) {
			error_out = "No models found in server response";
		}
	} catch (const std::exception &e) {
		error_out = std::format("Exception: {}", e.what());
	}

	return result;
}

} // namespace agentlib
