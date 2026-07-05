#include <filesystem>
#include <fstream>
#include <future>
#include <regex>
#include <curl/curl.h>
#include "agentlib/tool_context.h"
#include "fs_utils.h"
#include "images/image_manager.h"
#include "image_import.h"

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
		return "curl: (" + std::to_string(res) + ") " + curl_easy_strerror(res);
	}

	return read_buffer;
}

static std::string clean_alias_name(const std::string &uri)
{
	if (!uri.starts_with("images://")) {
		return uri;
	}
	std::string clean = uri.substr(9);
	if (clean.starts_with("by-name/")) {
		clean = clean.substr(8);
	}
	return clean;
}

image_import_tool::image_import_tool(image_import_args args)
    : llm_tool_action("Importing image"), args_(std::move(args))
{
}

bool image_import_tool::validate_runtime(const agentlib::tool_context & /*ctx*/, std::string & /*out_error*/) const
{
	return true;
}

std::string image_import_tool::execute(agentlib::tool_context &ctx)
{
	std::string cleaned_output = clean_alias_name(args_.output);

	if (args_.filename.has_value()) {
		// Import from local file
		std::string new_uri = images::image_manager::get_instance().ingest_image(*args_.filename, cleaned_output);
		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest local image: " + *args_.filename);
			return "Error: Failed to ingest local image into VFS database.";
		}
		set_success(ctx, "Imported image " + *args_.filename + " as " + args_.output);
		return "Successfully imported local image as VFS URI: " + new_uri;
	}

	if (args_.URL.has_value()) {
		// Import from URL
		std::string domain = extract_domain(*args_.URL);
		if (domain.empty()) {
			set_failure(ctx, "Invalid URL domain");
			return "Error: Could not parse domain from URL: " + *args_.URL;
		}

		std::string cache_dir = fs_utils::get_global_cache_dir();
		std::filesystem::path domains_file = std::filesystem::path(cache_dir) / "allowed_domains.txt";

		char rule = '?';
		std::ifstream in(domains_file);
		if (in) {
			std::string line;
			while (std::getline(in, line)) {
				if (line.length() > 2 && line[1] == ':') {
					if (line.substr(2) == domain) {
						rule = line[0];
						break;
					}
				}
			}
		}

		if (rule == 'D') {
			set_failure(ctx, "Domain blocked: " + domain);
			return "Error: Permission denied to access domain: " + domain + " (Blacklisted)";
		}

		if (rule != 'A') {
			if (!ctx.queue) {
				set_failure(ctx, "No event queue to prompt for network access");
				return "Error: No event queue available to prompt for network permission.";
			}

			auto promise = std::make_shared<std::promise<std::string>>();
			auto future = promise->get_future();

			editor_event ev;
			ev.type = event_type::prompt_user;
			ev.payload = "Agent wants to import an image from URL:\n" + *args_.URL + "\n\nAllow connection to " + domain + "?";

			bool is_local = is_local_ip(domain);
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
				set_failure(ctx, e.what());
				return std::string("Error: Failed to get user response - ") + e.what();
			}

			if (response == "Deny") {
				set_failure(ctx, "Permission denied");
				return "Error: Permission denied by user for this request.";
			} else if (response == "Deny Always") {
				std::ofstream out(domains_file, std::ios::app);
				out << "D:" << domain << "\n";
				set_failure(ctx, "Permission denied (domain blacklisted)");
				return "Error: Permission denied by user (Blacklisted).";
			} else if (response == "Always") {
				std::ofstream out(domains_file, std::ios::app);
				out << "A:" << domain << "\n";
			} else if (response != "Once") {
				set_failure(ctx, "Unknown user response");
				return "Error: Unknown response from user.";
			}
		}

		std::string downloaded_data = perform_http_get(*args_.URL);
		if (downloaded_data.starts_with("Error:") || downloaded_data.starts_with("curl:")) {
			set_failure(ctx, downloaded_data);
			return downloaded_data;
		}

		// Save downloaded data to temp file first to ingest it
		std::string temp_out = images::image_manager::get_instance().get_temp_image_path();
		std::ofstream ofs(temp_out, std::ios::binary);
		if (!ofs.is_open()) {
			set_failure(ctx, "Failed to write downloaded data to temp file");
			return "Error: Failed to save downloaded image to temporary location.";
		}
		ofs.write(downloaded_data.data(), downloaded_data.size());
		ofs.close();

		std::string new_uri = images::image_manager::get_instance().ingest_image(temp_out, cleaned_output);
		std::filesystem::remove(temp_out); // Cleanup temp file

		if (new_uri.empty()) {
			set_failure(ctx, "Failed to ingest VFS image from downloaded data");
			return "Error: Failed to register downloaded image into VFS database.";
		}

		set_success(ctx, "Downloaded and imported " + *args_.URL + " as " + args_.output);
		return "Successfully downloaded and imported VFS URI: " + new_uri;
	}

	set_failure(ctx, "No source specified");
	return "Error: Missing source file or URL.";
}

} // namespace tools
