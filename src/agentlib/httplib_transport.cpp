#include "httplib_transport.h"
#include <httplib.h>

namespace agentlib
{

httplib_transport::httplib_transport(const std::string &base_url) : base_url_(base_url)
{
	cli_ = std::make_unique<httplib::Client>(base_url_);
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
		res = cli_->Post(path.c_str(), json_body, "application/json");
	}

	transport_response response;
	if (res) {
		response.status_code = res->status;
		response.body = res->body;
	} else {
		response.status_code = -1;
		response.body = "Connection failed or cancelled";
	}
	return response;
}

bool httplib_transport::post_stream(const std::string &path, const std::string &json_body,
				    std::function<bool(const char *data, size_t len, size_t off, size_t total)> callback)
{
	httplib::Result res;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!cli_)
			return false;

		httplib::Request req;
		req.method = "POST";
		req.path = path;
		req.body = json_body;
		req.set_header("Content-Type", "application/json");
		req.content_receiver = callback;

		res = cli_->send(req);
	}
	return res && res->status == 200;
}

} // namespace agentlib
