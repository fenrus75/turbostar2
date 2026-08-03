#include "image_manager.h"
#include "fs_utils.h"
#include "mime.h"
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace images
{

static std::string compute_file_sha256(const std::string &filepath)
{
	std::ifstream file(filepath, std::ios::binary);
	if (!file) {
		return "";
	}

	EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
	if (!mdctx) return "";

	if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
		EVP_MD_CTX_free(mdctx);
		return "";
	}

	char buffer[32768];
	while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
		if (EVP_DigestUpdate(mdctx, buffer, file.gcount()) != 1) {
			EVP_MD_CTX_free(mdctx);
			return "";
		}
	}

	unsigned char hash[EVP_MAX_MD_SIZE];
	unsigned int hash_len = 0;
	if (EVP_DigestFinal_ex(mdctx, hash, &hash_len) != 1) {
		EVP_MD_CTX_free(mdctx);
		return "";
	}

	EVP_MD_CTX_free(mdctx);

	std::stringstream ss;
	for (unsigned int i = 0; i < hash_len; i++) {
		ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	}
	return ss.str();
}

static bool parse_png_dimensions(const std::string &filepath, int &width, int &height)
{
	std::ifstream file(filepath, std::ios::binary);
	if (!file) return false;

	unsigned char sig[8];
	if (!file.read(reinterpret_cast<char*>(sig), 8)) return false;

	if (sig[0] != 0x89 || sig[1] != 'P' || sig[2] != 'N' || sig[3] != 'G' ||
	    sig[4] != '\r' || sig[5] != '\n' || sig[6] != 0x1a || sig[7] != '\n') {
		return false;
	}

	unsigned char ihdr_header[8];
	if (!file.read(reinterpret_cast<char*>(ihdr_header), 8)) return false;

	if (ihdr_header[4] != 'I' || ihdr_header[5] != 'H' || ihdr_header[6] != 'D' || ihdr_header[7] != 'R') {
		return false;
	}

	unsigned char dims[8];
	if (!file.read(reinterpret_cast<char*>(dims), 8)) return false;

	width = (dims[0] << 24) | (dims[1] << 16) | (dims[2] << 8) | dims[3];
	height = (dims[4] << 24) | (dims[5] << 16) | (dims[6] << 8) | dims[7];
	return true;
}

static bool parse_jpeg_dimensions(const std::string &filepath, int &width, int &height)
{
	std::ifstream file(filepath, std::ios::binary);
	if (!file) return false;

	unsigned char sig[2];
	if (!file.read(reinterpret_cast<char*>(sig), 2)) return false;

	if (sig[0] != 0xff || sig[1] != 0xd8) return false;

	while (true) {
		unsigned char marker_header[2];
		if (!file.read(reinterpret_cast<char*>(marker_header), 2)) break;

		if (marker_header[0] != 0xff) {
			while (marker_header[0] != 0xff && !file.eof()) {
				file.read(reinterpret_cast<char*>(marker_header), 1);
			}
			if (file.eof()) break;
			file.read(reinterpret_cast<char*>(&marker_header[1]), 1);
		}

		unsigned char marker = marker_header[1];
		if (marker == 0xd9 || marker == 0xda) {
			break;
		}

		unsigned char len_bytes[2];
		if (!file.read(reinterpret_cast<char*>(len_bytes), 2)) break;
		unsigned short len = (len_bytes[0] << 8) | len_bytes[1];

		if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
			unsigned char sof_data[5];
			if (!file.read(reinterpret_cast<char*>(sof_data), 5)) break;

			height = (sof_data[1] << 8) | sof_data[2];
			width = (sof_data[3] << 8) | sof_data[4];
			return true;
		} else {
			if (len < 2) break;
			file.seekg(len - 2, std::ios::cur);
		}
	}
	return false;
}

static std::string detect_mime_type(const std::string &filepath)
{
	return mime::detect_file_type(filepath);
}

image_manager &image_manager::get_instance()
{
	static image_manager instance;
	return instance;
}

void image_manager::initialize()
{
	load_mappings();
}

std::string image_manager::get_cache_dir()
{
	auto path = std::filesystem::path(fs_utils::get_project_cache_root()) / "images";
	std::error_code ec;
	std::filesystem::create_directories(path, ec);
	return path.string();
}

std::string image_manager::get_mappings_path()
{
	return (std::filesystem::path(get_cache_dir()) / "mappings.json").string();
}

void image_manager::load_mappings()
{
	std::lock_guard<std::mutex> lock(mutex_);
	mappings_.clear();

	std::string path = get_mappings_path();
	if (!std::filesystem::exists(path)) {
		return;
	}

	std::ifstream ifs(path);
	if (!ifs) {
		return;
	}

	try {
		nlohmann::json j;
		ifs >> j;
		if (j.contains("mappings") && j["mappings"].is_array()) {
			for (const auto &item : j["mappings"]) {
				image_metadata meta;
				meta.sha256 = item.value("sha256", "");
				if (item.contains("file_ids") && item["file_ids"].is_array()) {
					for (const auto &fid : item["file_ids"]) {
						meta.file_ids.push_back(fid.get<std::string>());
					}
				}
				if (item.contains("names") && item["names"].is_array()) {
					for (const auto &name : item["names"]) {
						meta.names.push_back(name.get<std::string>());
					}
				}
				meta.created_at = item.value("created_at", 0LL);
				meta.mime_type = item.value("mime_type", "image/octet-stream");
				meta.width = item.value("width", 0);
				meta.height = item.value("height", 0);
				meta.size_bytes = item.value("size_bytes", 0UL);
				meta.origin_file = item.value("origin_file", "");
				meta.origin_ops = item.value("origin_ops", "");
				mappings_.push_back(meta);
			}
		}
	} catch (...) {
		// Ignore parse errors, start with clean list
	}
}

void image_manager::save_mappings()
{
	std::string path = get_mappings_path();
	std::ofstream ofs(path);
	if (!ofs) {
		return;
	}

	nlohmann::json j_list = nlohmann::json::array();
	for (const auto &meta : mappings_) {
		nlohmann::json item;
		item["sha256"] = meta.sha256;
		item["file_ids"] = meta.file_ids;
		item["names"] = meta.names;
		item["created_at"] = meta.created_at;
		item["mime_type"] = meta.mime_type;
		item["width"] = meta.width;
		item["height"] = meta.height;
		item["size_bytes"] = meta.size_bytes;
		item["origin_file"] = meta.origin_file;
		item["origin_ops"] = meta.origin_ops;
		j_list.push_back(item);
	}

	nlohmann::json out;
	out["mappings"] = j_list;
	ofs << out.dump(4);
}

std::string image_manager::get_canonical_sha256(const std::string &uri)
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (uri.empty()) {
		return "";
	}

	std::string clean_uri = uri;
	if (clean_uri.starts_with("images://")) {
		clean_uri = clean_uri.substr(9);
	}

	if (clean_uri.starts_with("by-sha256/")) {
		return clean_uri.substr(10);
	}

	if (clean_uri.starts_with("by-file-id/")) {
		std::string file_id = clean_uri.substr(11);
		for (const auto &meta : mappings_) {
			if (std::find(meta.file_ids.begin(), meta.file_ids.end(), file_id) != meta.file_ids.end()) {
				return meta.sha256;
			}
		}
		return "";
	}

	std::string name = clean_uri;
	if (name.starts_with("by-name/")) {
		name = name.substr(8);
	}
	for (const auto &meta : mappings_) {
		if (std::find(meta.names.begin(), meta.names.end(), name) != meta.names.end()) {
			return meta.sha256;
		}
	}

	for (const auto &meta : mappings_) {
		if (meta.sha256 == clean_uri) {
			return meta.sha256;
		}
	}

	return "";
}

std::string image_manager::resolve_uri(const std::string &uri)
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (!uri.starts_with("images://")) {
		return "";
	}

	std::string clean_uri = uri.substr(9); // Strip "images://"
	std::string target_hash;

	if (clean_uri.starts_with("by-sha256/")) {
		target_hash = clean_uri.substr(10);
	} else if (clean_uri.starts_with("by-file-id/")) {
		std::string file_id = clean_uri.substr(11);
		for (const auto &meta : mappings_) {
			if (std::find(meta.file_ids.begin(), meta.file_ids.end(), file_id) != meta.file_ids.end()) {
				target_hash = meta.sha256;
				break;
			}
		}
	} else {
		std::string name = clean_uri;
		if (name.starts_with("by-name/")) {
			name = name.substr(8);
		}
		for (const auto &meta : mappings_) {
			if (std::find(meta.names.begin(), meta.names.end(), name) != meta.names.end()) {
				target_hash = meta.sha256;
				break;
			}
		}
	}

	if (target_hash.empty()) {
		return "";
	}

	auto physical_path = std::filesystem::path(get_cache_dir()) / target_hash;
	if (std::filesystem::exists(physical_path)) {
		return physical_path.string();
	}

	return "";
}

std::string image_manager::get_temp_image_path()
{
	auto tmp_dir = std::filesystem::path(fs_utils::get_project_tmp_dir());
	auto filename = "img_tmp_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".tmp";
	return (tmp_dir / filename).string();
}

std::string image_manager::ingest_image(
    const std::string &temp_path,
    const std::string &alias,
    const std::string &origin_file,
    const std::string &origin_ops)
{
	if (!std::filesystem::exists(temp_path)) {
		return "";
	}

	std::string hash = compute_file_sha256(temp_path);
	if (hash.empty()) {
		return "";
	}

	std::string canonical_origin = get_canonical_sha256(origin_file);
	if (canonical_origin.empty() && !origin_file.empty()) {
		canonical_origin = origin_file;
	}

	std::string cache_dir = get_cache_dir();
	auto dest_path = std::filesystem::path(cache_dir) / hash;

	try {
		std::filesystem::copy_file(temp_path, dest_path, std::filesystem::copy_options::overwrite_existing);
//		std::filesystem::remove(temp_path);
	} catch (...) {
		return "";
	}

	int width = 0;
	int height = 0;
	std::string mime = detect_mime_type(dest_path.string());
	size_t size = std::filesystem::file_size(dest_path);

	if (mime == "image/png") {
		parse_png_dimensions(dest_path.string(), width, height);
	} else if (mime == "image/jpeg") {
		parse_jpeg_dimensions(dest_path.string(), width, height);
	}

	std::string clean_alias = alias;
	if (clean_alias.starts_with("images://")) {
		clean_alias = clean_alias.substr(9);
	}
	if (clean_alias.starts_with("by-name/")) {
		clean_alias = clean_alias.substr(8);
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!clean_alias.empty()) {
			for (auto &meta : mappings_) {
				if (meta.sha256 != hash) {
					auto name_it = std::find(meta.names.begin(), meta.names.end(), clean_alias);
					if (name_it != meta.names.end()) {
						meta.names.erase(name_it);
					}
				}
			}
		}

		auto it = std::find_if(mappings_.begin(), mappings_.end(), [&](const image_metadata &m) {
			return m.sha256 == hash;
		});

		if (it != mappings_.end()) {
			if (!clean_alias.empty() && std::find(it->names.begin(), it->names.end(), clean_alias) == it->names.end()) {
				it->names.push_back(clean_alias);
			}
			if (!canonical_origin.empty()) {
				it->origin_file = canonical_origin;
			}
			if (!origin_ops.empty()) {
				it->origin_ops = origin_ops;
			}
		} else {
			image_metadata meta;
			meta.sha256 = hash;
			if (!clean_alias.empty()) {
				meta.names.push_back(clean_alias);
			}
			meta.created_at = std::chrono::duration_cast<std::chrono::seconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			meta.mime_type = mime;
			meta.width = width;
			meta.height = height;
			meta.size_bytes = size;
			meta.origin_file = canonical_origin;
			meta.origin_ops = origin_ops;
			mappings_.push_back(meta);
		}

		save_mappings();
	}

	return "images://by-sha256/" + hash;
}

std::vector<origin_chain_node> image_manager::get_origin_chain(const std::string &uri)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<origin_chain_node> chain;

	auto resolve_hash_locked = [&](const std::string &u) -> std::string {
		if (u.empty()) return "";
		std::string clean = u;
		if (clean.starts_with("images://")) clean = clean.substr(9);
		if (clean.starts_with("by-sha256/")) return clean.substr(10);
		if (clean.starts_with("by-file-id/")) {
			std::string fid = clean.substr(11);
			for (const auto &m : mappings_) {
				if (std::find(m.file_ids.begin(), m.file_ids.end(), fid) != m.file_ids.end())
					return m.sha256;
			}
			return "";
		}
		std::string n = clean;
		if (n.starts_with("by-name/")) n = n.substr(8);
		for (const auto &m : mappings_) {
			if (std::find(m.names.begin(), m.names.end(), n) != m.names.end())
				return m.sha256;
		}
		for (const auto &m : mappings_) {
			if (m.sha256 == clean) return m.sha256;
		}
		return "";
	};

	std::string current_hash = resolve_hash_locked(uri);
	if (current_hash.empty()) {
		return chain;
	}

	size_t depth = 0;
	std::vector<std::string> visited;

	while (!current_hash.empty() && depth < 32) {
		if (std::find(visited.begin(), visited.end(), current_hash) != visited.end()) {
			break;
		}
		visited.push_back(current_hash);

		auto it = std::find_if(mappings_.begin(), mappings_.end(), [&](const image_metadata &m) {
			return m.sha256 == current_hash;
		});

		if (it == mappings_.end()) {
			break;
		}

		std::string display_name = current_hash.substr(0, 8);
		if (!it->names.empty()) {
			display_name = it->names.front();
		}

		chain.push_back(origin_chain_node{it->sha256, display_name, it->origin_ops});
		current_hash = it->origin_file;
		depth++;
	}

	std::reverse(chain.begin(), chain.end());
	return chain;
}

std::string image_manager::format_origin_chain(const std::string &uri)
{
	auto chain = get_origin_chain(uri);
	if (chain.empty()) {
		return "";
	}

	std::string result;
	for (size_t i = 0; i < chain.size(); ++i) {
		if (i > 0) {
			if (!chain[i].origin_ops.empty()) {
				result += " -> " + chain[i].origin_ops + " -> ";
			} else {
				result += " -> ";
			}
		}
		std::string name_str = chain[i].name;
		if (!name_str.starts_with("images://")) {
			name_str = "images://" + name_str;
		}
		result += name_str;
	}
	return result;
}

bool image_manager::get_metadata(const std::string &uri, image_metadata &out_meta)
{
	std::string physical_path = resolve_uri(uri);
	if (physical_path.empty()) {
		return false;
	}

	std::filesystem::path p(physical_path);
	std::string hash = p.filename().string();

	std::lock_guard<std::mutex> lock(mutex_);
	for (const auto &meta : mappings_) {
		if (meta.sha256 == hash) {
			out_meta = meta;
			return true;
		}
	}

	return false;
}

bool image_manager::register_file_id(const std::string &uri, const std::string &file_id)
{
	std::string physical_path = resolve_uri(uri);
	if (physical_path.empty()) {
		return false;
	}

	std::filesystem::path p(physical_path);
	std::string hash = p.filename().string();

	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &meta : mappings_) {
		if (meta.sha256 == hash) {
			if (std::find(meta.file_ids.begin(), meta.file_ids.end(), file_id) == meta.file_ids.end()) {
				meta.file_ids.push_back(file_id);
				save_mappings();
			}
			return true;
		}
	}

	return false;
}

void image_manager::clear_cache()
{
	std::lock_guard<std::mutex> lock(mutex_);
	mappings_.clear();

	std::string cache_dir = get_cache_dir();
	try {
		std::filesystem::remove_all(cache_dir);
		std::filesystem::create_directories(cache_dir);
	} catch (...) {
		// Ignore filesystem issues on clearance
	}

	save_mappings();
}

std::vector<image_metadata> image_manager::get_all_mappings()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return mappings_;
}

bool image_manager::delete_image(const std::string &uri)
{
	std::string physical_path = resolve_uri(uri);
	if (physical_path.empty()) {
		return false;
	}

	std::filesystem::path p(physical_path);
	std::string hash = p.filename().string();

	std::lock_guard<std::mutex> lock(mutex_);
	auto it = std::find_if(mappings_.begin(), mappings_.end(), [&](const image_metadata &m) {
		return m.sha256 == hash;
	});

	if (it != mappings_.end()) {
		mappings_.erase(it);
		std::error_code ec;
		std::filesystem::remove(physical_path, ec);
		save_mappings();
		return true;
	}

	return false;
}

} // namespace images