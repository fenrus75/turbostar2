#include "virtual_file_system.h"
#include "images/image_manager.h"
#include "fs_utils.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <curl/curl.h>
#include <fcntl.h>
#include <format>
#include <nlohmann/json.hpp>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace agentlib
{

namespace
{
struct curl_global_guard {
	curl_global_guard()
	{
		curl_global_init(CURL_GLOBAL_DEFAULT);
	}
	~curl_global_guard()
	{
		curl_global_cleanup();
	}
};
static curl_global_guard g_curl_global_guard;
} // namespace

static size_t count_lines(const void *data, size_t size)
{
	if (size == 0 || !data) {
		return 0;
	}

	size_t lines = 0;
	const char *p = static_cast<const char *>(data);
	const char *end = p + size;

	while (p < end) {
		const char *next = static_cast<const char *>(memchr(p, '\n', end - p));
		if (next == nullptr) {
			break;
		}
		lines++;
		p = next + 1;
	}

	if (size > 0 && *(end - 1) != '\n') {
		lines++;
	}

	return lines;
}

mmap_handle::~mmap_handle()
{
	if (data && data != MAP_FAILED && size > 0) {
		if (type == 'F') {
			munmap(data, size);
		} else if (type == 'M') {
			free(data);
		}
	}
}

void memory_vfs_provider::ensure_directories_exist(const std::string &file_uri)
{
	size_t scheme_pos = file_uri.find("://");
	if (scheme_pos == std::string::npos)
		return;

	size_t start = scheme_pos + 3;
	size_t slash_pos;

	while ((slash_pos = file_uri.find('/', start)) != std::string::npos) {
		std::string dir_uri = file_uri.substr(0, slash_pos + 1);

		if (!mounts_.contains(dir_uri)) {
			auto handle = std::make_shared<mmap_handle>();
			handle->type = 'D';
			mounts_[dir_uri] = std::move(handle);
		}
		start = slash_pos + 1;
	}
}

bool memory_vfs_provider::mount_file(const std::string &uri, const std::string &disk_path)
{
	int fd = open(disk_path.c_str(), O_RDONLY);
	if (fd < 0) {
		return false;
	}

	struct stat sb;
	if (fstat(fd, &sb) < 0) {
		close(fd);
		return false;
	}

	if (sb.st_size == 0) {
		close(fd);
		auto handle = std::make_shared<mmap_handle>();
		handle->size = 0;
		mounts_[uri] = std::move(handle);
		return true;
	}

	void *mapped = mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);

	if (mapped == MAP_FAILED) {
		return false;
	}

	auto handle = std::make_shared<mmap_handle>();
	handle->data = mapped;
	handle->size = sb.st_size;
	handle->type = 'F';
	handle->size_in_lines = count_lines(mapped, sb.st_size);

	ensure_directories_exist(uri);
	mounts_[uri] = std::move(handle);
	return true;
}

bool memory_vfs_provider::mount_buffer(const std::string &uri, const std::string &buffer)
{
	auto handle = std::make_shared<mmap_handle>();
	handle->type = 'M';
	handle->size = buffer.size();

	if (handle->size > 0) {
		handle->data = malloc(handle->size);
		if (!handle->data)
			return false;
		memcpy(handle->data, buffer.data(), handle->size);
	}

	handle->size_in_lines = count_lines(handle->data, handle->size);

	ensure_directories_exist(uri);
	mounts_[uri] = std::move(handle);
	return true;
}

void memory_vfs_provider::unmount_file(const std::string &uri)
{
	mounts_.erase(uri);
}

void memory_vfs_provider::unmount_prefix(const std::string &prefix)
{
	auto it = mounts_.lower_bound(prefix);
	while (it != mounts_.end() && it->first.starts_with(prefix)) {
		it = mounts_.erase(it);
	}
}

bool memory_vfs_provider::exists(const std::string &uri) const
{
	return mounts_.contains(uri);
}

std::optional<vfs_file_handle> memory_vfs_provider::read_file(const std::string &uri)
{
	auto it = mounts_.find(uri);
	if (it != mounts_.end()) {
		return std::make_shared<mmap_content_buffer>(it->second);
	}
	return std::nullopt;
}

std::optional<vfs_file_info> memory_vfs_provider::get_file_info(const std::string &uri) const
{
	auto it = mounts_.find(uri);
	if (it != mounts_.end()) {
		char exposed_type = it->second->type;
		if (exposed_type == 'M') {
			exposed_type = 'F';
		}
		return vfs_file_info{uri, it->second->size, exposed_type, it->second->size_in_lines};
	}
	return std::nullopt;
}

std::vector<vfs_file_info> memory_vfs_provider::list_directory(const std::string &prefix) const
{
	std::vector<vfs_file_info> results;
	auto it = mounts_.lower_bound(prefix);
	while (it != mounts_.end() && it->first.starts_with(prefix)) {
		char exposed_type = it->second->type;
		if (exposed_type == 'M') {
			exposed_type = 'F';
		}
		results.push_back({it->first, it->second->size, exposed_type, it->second->size_in_lines});
		++it;
	}
	return results;
}

virtual_file_system::virtual_file_system()
{
	default_provider_ = std::make_shared<memory_vfs_provider>();
	register_provider("skills", default_provider_);
	register_provider("agent", default_provider_);

	auto github_prov = std::make_shared<github_vfs_provider>();
	register_provider("github", github_prov);

	auto file_prov = std::make_shared<file_vfs_provider>();
	register_provider("file", file_prov);

	auto images_prov = std::make_shared<images_vfs_provider>();
	register_provider("images", images_prov);
}

bool virtual_file_system::mount_file(const std::string &uri, const std::string &disk_path)
{
	return default_provider_->mount_file(uri, disk_path);
}

bool virtual_file_system::mount_buffer(const std::string &uri, const std::string &buffer)
{
	return default_provider_->mount_buffer(uri, buffer);
}

void virtual_file_system::unmount_file(const std::string &uri)
{
	default_provider_->unmount_file(uri);
}

void virtual_file_system::unmount_prefix(const std::string &prefix)
{
	default_provider_->unmount_prefix(prefix);
}

std::shared_ptr<vfs_provider> virtual_file_system::get_provider_for_uri(const std::string &uri) const
{
	size_t scheme_pos = uri.find("://");
	if (scheme_pos != std::string::npos) {
		std::string scheme = uri.substr(0, scheme_pos);
		auto it = providers_.find(scheme);
		if (it != providers_.end()) {
			return it->second;
		}
	}
	return default_provider_;
}

bool virtual_file_system::exists(const std::string &uri) const
{
	return get_provider_for_uri(uri)->exists(uri);
}

std::optional<vfs_file_handle> virtual_file_system::read_file(const std::string &uri)
{
	return get_provider_for_uri(uri)->read_file(uri);
}

std::optional<vfs_file_info> virtual_file_system::get_file_info(const std::string &uri) const
{
	return get_provider_for_uri(uri)->get_file_info(uri);
}

std::vector<vfs_file_info> virtual_file_system::list_directory(const std::string &prefix) const
{
	return get_provider_for_uri(prefix)->list_directory(prefix);
}

void virtual_file_system::register_provider(const std::string &scheme, std::shared_ptr<vfs_provider> provider)
{
	providers_[scheme] = provider;
}

std::optional<vfs_write_handle> virtual_file_system::create_file(const std::string &uri)
{
	auto provider = get_provider_for_uri(uri);
	if (provider) {
		return provider->create_file(uri);
	}
	return std::nullopt;
}

std::string virtual_file_system::write_file(const std::string &uri, const void *data, size_t size)
{
	auto provider = get_provider_for_uri(uri);
	if (provider) {
		return provider->write_file(uri, data, size);
	}
	return "";
}

// -------------------------------------------------------------------------
// github_vfs_provider Implementation
// -------------------------------------------------------------------------

bool github_vfs_provider::exists(const std::string &uri) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return exists_unlocked(uri);
}

bool github_vfs_provider::exists_unlocked(const std::string &uri) const
{
	return get_file_info_unlocked(uri).has_value();
}

std::optional<vfs_file_handle> github_vfs_provider::read_file(const std::string &uri)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto parsed = parse_uri(uri);
	if (!parsed || parsed->is_user_only || parsed->is_repo_root) {
		return std::nullopt;
	}

	std::string branch = parsed->branch;
	if (branch.empty()) {
		branch = get_default_branch(parsed->owner, parsed->repo);
	}

	std::string cache_key = "github://" + parsed->owner + "/" + parsed->repo + "@" + branch + "/" + parsed->path;

	auto cached = cache_get(cache_key);
	if (cached) {
		return std::make_shared<string_content_buffer>(*cached);
	}

	std::string url = "https://raw.githubusercontent.com/" + parsed->owner + "/" + parsed->repo + "/" + branch + "/" + parsed->path;
	int status = 0;
	std::string body = http_get(url, status);
	if (status == 200) {
		cache_put(cache_key, body);
		return std::make_shared<string_content_buffer>(body);
	}

	return std::nullopt;
}

std::optional<vfs_file_info> github_vfs_provider::get_file_info(const std::string &uri) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return get_file_info_unlocked(uri);
}

std::optional<vfs_file_info> github_vfs_provider::get_file_info_unlocked(const std::string &uri) const
{
	auto parsed = parse_uri(uri);
	if (!parsed)
		return std::nullopt;

	if (parsed->is_user_only) {
		return vfs_file_info{uri, 0, 'D', 0};
	}

	if (parsed->is_repo_root) {
		return vfs_file_info{uri, 0, 'D', 0};
	}

	std::string branch = parsed->branch;
	if (branch.empty()) {
		branch = get_default_branch(parsed->owner, parsed->repo);
	}

	// First, check if the parent directory is already cached
	std::string parent_uri = "github://" + parsed->owner + "/" + parsed->repo;
	if (!parsed->branch.empty()) {
		parent_uri += "@" + branch;
	}
	parent_uri += "/";

	size_t last_slash = parsed->path.rfind('/');
	if (last_slash != std::string::npos) {
		parent_uri += parsed->path.substr(0, last_slash + 1);
	}

	auto dir_it = dir_cache_.find(parent_uri);
	if (dir_it != dir_cache_.end()) {
		for (const auto &item : dir_it->second) {
			if (item.uri == uri || item.uri == uri + "/") {
				return item;
			}
		}
	}

	// Direct lookup if not cached
	std::string url =
	    "https://api.github.com/repos/" + parsed->owner + "/" + parsed->repo + "/contents/" + parsed->path + "?ref=" + branch;
	int status = 0;
	std::string body = http_get(url, status);
	if (status == 200) {
		try {
			auto j = nlohmann::json::parse(body);
			if (j.is_array()) {
				return vfs_file_info{uri, 0, 'D', 0};
			} else if (j.is_object()) {
				size_t size = j.value("size", 0UL);
				std::string type_str = j.value("type", "file");
				char type = (type_str == "dir") ? 'D' : 'F';
				return vfs_file_info{uri, size, type, 0};
			}
		} catch (const std::exception &e) {
			// Log silently
		}
	}

	return std::nullopt;
}

std::vector<vfs_file_info> github_vfs_provider::list_directory(const std::string &prefix) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto parsed = parse_uri(prefix);
	if (!parsed)
		return {};

	std::string norm_prefix = prefix;
	if (!norm_prefix.empty() && norm_prefix.back() != '/') {
		norm_prefix += '/';
	}

	auto it = dir_cache_.find(norm_prefix);
	if (it != dir_cache_.end()) {
		return it->second;
	}

	std::vector<vfs_file_info> results;

	if (parsed->is_user_only) {
		std::string url = "https://api.github.com/users/" + parsed->owner + "/repos";
		int status = 0;
		std::string body = http_get(url, status);
		if (status == 200) {
			try {
				auto j = nlohmann::json::parse(body);
				if (j.is_array()) {
					for (const auto &repo : j) {
						std::string repo_name = repo.at("name").get<std::string>();
						std::string item_uri = norm_prefix + repo_name;
						results.push_back(vfs_file_info{item_uri, 0, 'D', 0});
					}
				}
			} catch (const std::exception &e) {
				// Log silently
			}
		}
	} else {
		std::string branch = parsed->branch;
		if (branch.empty()) {
			branch = get_default_branch(parsed->owner, parsed->repo);
		}

		std::string url =
		    "https://api.github.com/repos/" + parsed->owner + "/" + parsed->repo + "/contents/" + parsed->path + "?ref=" + branch;
		int status = 0;
		std::string body = http_get(url, status);
		if (status == 200) {
			try {
				auto j = nlohmann::json::parse(body);
				if (j.is_array()) {
					for (const auto &item : j) {
						std::string name = item.value("name", "");
						std::string item_type_str = item.value("type", "file");
						char type = (item_type_str == "dir") ? 'D' : 'F';
						size_t size = item.value("size", 0UL);

						std::string item_uri = norm_prefix + name;
						results.push_back(vfs_file_info{item_uri, size, type, 0});
					}
				}
			} catch (const std::exception &e) {
				// Log silently
			}
		}
	}

	if (!results.empty()) {
		dir_cache_[norm_prefix] = results;
	}

	return results;
}

std::optional<github_vfs_provider::github_uri> github_vfs_provider::parse_uri(const std::string &uri) const
{
	if (!uri.starts_with("github://")) {
		return std::nullopt;
	}

	std::string inner = uri.substr(9);
	while (!inner.empty() && inner.front() == '/') {
		inner = inner.substr(1);
	}
	while (!inner.empty() && inner.back() == '/') {
		inner.pop_back();
	}

	if (inner.empty()) {
		return std::nullopt;
	}

	github_uri res;
	size_t slash1 = inner.find('/');
	if (slash1 == std::string::npos) {
		res.owner = inner;
		res.is_user_only = true;
		return res;
	}

	res.owner = inner.substr(0, slash1);
	std::string repo_part = inner.substr(slash1 + 1);

	size_t slash2 = repo_part.find('/');
	std::string repo_name_with_branch;
	if (slash2 == std::string::npos) {
		repo_name_with_branch = repo_part;
		res.is_repo_root = true;
	} else {
		repo_name_with_branch = repo_part.substr(0, slash2);
		res.path = repo_part.substr(slash2 + 1);
	}

	size_t at_pos = repo_name_with_branch.find('@');
	if (at_pos != std::string::npos) {
		res.repo = repo_name_with_branch.substr(0, at_pos);
		res.branch = repo_name_with_branch.substr(at_pos + 1);
	} else {
		res.repo = repo_name_with_branch;
	}

	return res;
}

static size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	size_t realsize = size * nmemb;
	std::string *mem = static_cast<std::string *>(userp);
	mem->append(static_cast<const char *>(contents), realsize);
	return realsize;
}

std::string github_vfs_provider::http_get(const std::string &url, int &out_status) const
{
	out_status = 0;
	if (!url.starts_with("https://")) {
		return "";
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		out_status = -1;
		return "";
	}

	std::string response_data;

	// Set URL
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

	// Follow redirects
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

	// User agent
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Turbostar/1.0");

	// Timeouts
	const char *in_testsuite = std::getenv("TURBOSTAR_IN_TESTSUITE");
	if (in_testsuite && std::string(in_testsuite) == "1") {
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 500L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 2000L);
	} else {
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	}

	// Set write callback
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);

	// Setup headers
	struct curl_slist *headers = nullptr;
	const char *token = std::getenv("GITHUB_TOKEN");
	if (token) {
		std::string auth_header = std::format("Authorization: Bearer {}", token);
		headers = curl_slist_append(headers, auth_header.c_str());
	}

	if (headers) {
		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	}

	// Perform request
	CURLcode res = curl_easy_perform(curl);
	if (res == CURLE_OK) {
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		out_status = static_cast<int>(http_code);
	} else {
		out_status = -1;
	}

	// Clean up
	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(curl);

	return response_data;
}

std::string github_vfs_provider::get_default_branch(const std::string &owner, const std::string &repo) const
{
	std::string key = owner + "/" + repo;
	auto it = branch_cache_.find(key);
	if (it != branch_cache_.end()) {
		return it->second;
	}

	std::string url = "https://api.github.com/repos/" + owner + "/" + repo;
	int status = 0;
	std::string body = http_get(url, status);
	if (status == 200) {
		try {
			auto j = nlohmann::json::parse(body);
			if (j.contains("default_branch")) {
				std::string db = j.at("default_branch").get<std::string>();
				branch_cache_[key] = db;
				return db;
			}
		} catch (const std::exception &e) {
			// Log silently
		}
	}

	return "main";
}

std::optional<std::string> github_vfs_provider::cache_get(const std::string &key) const
{
	auto it = file_cache_.find(key);
	if (it != file_cache_.end()) {
		update_lru(key);
		return it->second;
	}
	return std::nullopt;
}

void github_vfs_provider::cache_put(const std::string &key, const std::string &data) const
{
	if (file_cache_.size() >= 50) {
		if (!file_lru_.empty()) {
			std::string old_key = file_lru_.front();
			file_cache_.erase(old_key);
			file_lru_.erase(file_lru_.begin());
		}
	}
	file_cache_[key] = data;
	update_lru(key);
}

void github_vfs_provider::update_lru(const std::string &key) const
{
	auto it = std::find(file_lru_.begin(), file_lru_.end(), key);
	if (it != file_lru_.end()) {
		file_lru_.erase(it);
	}
	file_lru_.push_back(key);
}

// -------------------------------------------------------------------------
// file_vfs_provider Implementation
// -------------------------------------------------------------------------

std::string file_vfs_provider::parse_uri(const std::string &uri) const
{
	if (!uri.starts_with("file://")) {
		return "";
	}
	std::string path = uri.substr(7);
	if (path.starts_with("localhost/")) {
		path = path.substr(9);
	}
	return path;
}

bool file_vfs_provider::exists(const std::string &uri) const
{
	std::string path = parse_uri(uri);
	if (path.empty()) return false;
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

std::optional<vfs_file_handle> file_vfs_provider::read_file(const std::string &uri)
{
	std::string path = parse_uri(uri);
	if (path.empty()) return std::nullopt;

	std::ifstream ifs(path, std::ios::binary);
	if (!ifs) return std::nullopt;

	std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return std::make_shared<string_content_buffer>(std::move(content));
}

std::optional<vfs_file_info> file_vfs_provider::get_file_info(const std::string &uri) const
{
	std::string path = parse_uri(uri);
	if (path.empty()) return std::nullopt;

	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) return std::nullopt;

	size_t size = std::filesystem::file_size(path, ec);
	if (ec) size = 0;

	char type = 'F';
	if (std::filesystem::is_directory(path, ec)) {
		type = 'D';
	}

	size_t lines = 0;
	if (type == 'F' && size > 0) {
		std::ifstream ifs(path, std::ios::binary);
		if (ifs) {
			std::string line;
			while (std::getline(ifs, line)) {
				lines++;
			}
		}
	}

	return vfs_file_info{uri, size, type, lines};
}

std::vector<vfs_file_info> file_vfs_provider::list_directory(const std::string &prefix) const
{
	std::string path = parse_uri(prefix);
	if (path.empty()) return {};

	std::vector<vfs_file_info> results;
	std::error_code ec;
	if (!std::filesystem::is_directory(path, ec)) return {};

	for (const auto &entry : std::filesystem::directory_iterator(path, ec)) {
		std::string entry_path = entry.path().string();
		std::string item_uri = "file://" + entry_path;

		size_t size = 0;
		char type = 'F';
		if (entry.is_directory(ec)) {
			type = 'D';
		} else {
			size = entry.file_size(ec);
		}

		results.push_back({item_uri, size, type, 0});
	}
	return results;
}

std::optional<vfs_write_handle> file_vfs_provider::create_file(const std::string &uri)
{
	std::string raw_path = parse_uri(uri);
	if (raw_path.empty()) {
		return std::nullopt;
	}

	std::filesystem::path target = std::filesystem::weakly_canonical(raw_path);
	std::filesystem::path project_root = std::filesystem::canonical(fs_utils::get_project_dir());

	std::string target_str = target.string();
	std::string root_str = project_root.string();
	if (!root_str.ends_with(std::filesystem::path::preferred_separator)) {
		root_str += std::filesystem::path::preferred_separator;
	}

	if (target_str != project_root.string() && !target_str.starts_with(root_str)) {
		return std::nullopt; // Block: Directory traversal escape hack detected
	}

	class file_vfs_writer : public vfs_writer
	{
		std::filesystem::path path_;
		std::ofstream ofs_;

	      public:
		explicit file_vfs_writer(std::filesystem::path path)
		    : path_(std::move(path)), ofs_(path_, std::ios::binary)
		{
		}

		bool write(const void *data, size_t size) override
		{
			if (!ofs_.is_open()) return false;
			ofs_.write(static_cast<const char *>(data), size);
			return !ofs_.bad();
		}

		std::string close() override
		{
			ofs_.close();
			if (ofs_.fail()) {
				return "";
			}
			return "Successfully wrote file to " + path_.string();
		}
	};

	return std::make_unique<file_vfs_writer>(target);
}

// -------------------------------------------------------------------------
// images_vfs_provider Implementation
// -------------------------------------------------------------------------

bool images_vfs_provider::exists(const std::string &uri) const
{
	images::image_metadata meta;
	return images::image_manager::get_instance().get_metadata(uri, meta);
}

std::optional<vfs_file_handle> images_vfs_provider::read_file(const std::string &uri)
{
	std::string resolved = images::image_manager::get_instance().resolve_uri(uri);
	if (resolved.empty()) {
		return std::nullopt;
	}

	std::ifstream ifs(resolved, std::ios::binary);
	if (!ifs) {
		return std::nullopt;
	}
	std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return std::make_shared<string_content_buffer>(std::move(content));
}

std::optional<vfs_file_info> images_vfs_provider::get_file_info(const std::string &uri) const
{
	images::image_metadata meta;
	if (!images::image_manager::get_instance().get_metadata(uri, meta)) {
		return std::nullopt;
	}
	return vfs_file_info{uri, meta.size_bytes, 'F', 0};
}

std::vector<vfs_file_info> images_vfs_provider::list_directory(const std::string &prefix) const
{
	std::vector<vfs_file_info> results;
	auto mappings = images::image_manager::get_instance().get_all_mappings();
	for (const auto &meta : mappings) {
		for (const auto &name : meta.names) {
			std::string item_uri = "images://" + name;
			if (item_uri.starts_with(prefix)) {
				results.push_back({item_uri, meta.size_bytes, 'F', 0});
			}
		}
	}
	return results;
}

std::optional<vfs_write_handle> images_vfs_provider::create_file(const std::string &uri)
{
	class image_vfs_writer : public vfs_writer
	{
		std::string temp_path_;
		std::string alias_;
		std::ofstream ofs_;

	      public:
		image_vfs_writer(std::string temp_path, std::string alias)
		    : temp_path_(temp_path), alias_(alias), ofs_(temp_path, std::ios::binary)
		{
		}

		bool write(const void *data, size_t size) override
		{
			if (!ofs_.is_open()) return false;
			ofs_.write(static_cast<const char *>(data), size);
			return !ofs_.bad();
		}

		std::string close() override
		{
			ofs_.close();
			std::string new_uri = images::image_manager::get_instance().ingest_image(temp_path_, alias_);
			if (new_uri.empty()) {
				return "";
			}
			return "Successfully decompressed and ingested image into VFS database. URI: " + new_uri;
		}
	};

	std::string temp_path = images::image_manager::get_instance().get_temp_image_path();
	std::string alias = uri.substr(9);
	if (alias.starts_with("by-name/")) {
		alias = alias.substr(8);
	}

	return std::make_unique<image_vfs_writer>(temp_path, alias);
}

} // namespace agentlib
