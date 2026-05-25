#include "httplib_transport.h"
#include <httplib.h>

namespace agentlib
{

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

httplib_transport::httplib_transport(const std::string &base_url, const std::string &api_key)
    : base_url_(base_url), api_key_(api_key)
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
	cli_->set_follow_location(true);
}

httplib_transport::~httplib_transport() = default;

void httplib_transport::cancel()
{
	std::lock_guard<std::mutex> lock(mutex_);
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
		response.status_code = -1;
		last_error_ = error_to_string(res.error());
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
		last_error_ = error_to_string(res.error()) + " (Path: " + requested_path + ")";
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

} // namespace agentlib
