#include "codereview_manager.h"
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include "command_runner.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "project_manager.h"

void to_json(nlohmann::json &j, const review_item &item)
{
	j = nlohmann::json{{"id", item.id},
			   {"datestamp", item.datestamp},
			   {"git_hash", item.git_hash},
			   {"summary", item.summary},
			   {"filename", item.filename},
			   {"line_number", item.line_number},
			   {"line_content", item.line_content},
			   {"state", item.state},
			   {"severity", item.severity},
			   {"description", item.description},
			   {"proposed_fix", item.proposed_fix},
			   {"resolved_in_commit", item.resolved_in_commit}};
}

void from_json(const nlohmann::json &j, review_item &item)
{
	j.at("id").get_to(item.id);
	j.at("datestamp").get_to(item.datestamp);
	j.at("git_hash").get_to(item.git_hash);
	j.at("summary").get_to(item.summary);
	j.at("filename").get_to(item.filename);
	j.at("line_number").get_to(item.line_number);
	j.at("line_content").get_to(item.line_content);
	j.at("state").get_to(item.state);
	j.at("severity").get_to(item.severity);
	j.at("description").get_to(item.description);
	j.at("proposed_fix").get_to(item.proposed_fix);
	j.at("resolved_in_commit").get_to(item.resolved_in_commit);
}

codereview_manager &codereview_manager::get_instance()
{
	static codereview_manager instance;
	return instance;
}

std::string codereview_manager::get_review_json_path() const
{
	std::string cache_root = fs_utils::get_project_cache_root();
	if (cache_root.empty()) {
		return "";
	}
	return (std::filesystem::path(cache_root) / "review.json").string();
}

void codereview_manager::load_project(const std::string &project_root_path)
{
	std::unique_lock lock(mutex_);
	project_root_path_ = project_root_path;
	items_.clear();
	next_id_ = 1;

	std::string path = get_review_json_path();
	if (path.empty() || !std::filesystem::exists(path)) {
		return;
	}

	try {
		std::ifstream f(path);
		if (!f.is_open()) {
			return;
		}
		nlohmann::json j;
		f >> j;
		if (j.contains("next_id")) {
			next_id_ = j["next_id"].get<int>();
		}
		if (j.contains("items") && j["items"].is_array()) {
			for (const auto &item_j : j["items"]) {
				items_.push_back(item_j.get<review_item>());
			}
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("Error loading code reviews: {}", e.what()));
	}
}

void codereview_manager::save_project() const
{
	std::shared_lock lock(mutex_);
	save_project_unlocked();
}

void codereview_manager::save_project_unlocked() const
{
	std::string path = get_review_json_path();
	if (path.empty()) {
		return;
	}

	try {
		nlohmann::json j;
		j["next_id"] = next_id_;
		j["items"] = nlohmann::json::array();
		for (const auto &item : items_) {
			j["items"].push_back(item);
		}

		std::ofstream f(path);
		if (f.is_open()) {
			f << j.dump(4);
		}
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("Error saving code reviews: {}", e.what()));
	}
}

std::string codereview_manager::get_current_git_hash() const
{
	std::string root = project_root_path_;
	if (root.empty()) {
		root = project_manager::get_instance().get_project_root();
	}
	if (root.empty()) {
		return "";
	}
	sync_command_runner runner;
	runner.apply_internal_profile();
	std::string result = runner.execute_and_get_output("git -C {} rev-parse HEAD 2>/dev/null", root);
	if (!result.empty() && result.back() == '\n') {
		result.pop_back();
	}
	return result;
}

int codereview_manager::create_code_review_item(const std::string &summary, const std::string &filename, int line_number,
						const std::string &line_content, const std::string &severity,
						const std::string &description, const std::string &proposed_fix)
{
	std::unique_lock lock(mutex_);
	review_item item;
	item.id = next_id_++;
	item.datestamp = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	item.git_hash = get_current_git_hash();
	item.summary = summary;
	item.filename = filename;
	item.line_number = line_number;
	item.line_content = line_content;
	item.state = "new";
	item.severity = severity;
	item.description = description;
	item.proposed_fix = proposed_fix;

	items_.push_back(item);
	save_project_unlocked();
	return item.id;
}

bool codereview_manager::update_code_review_item(int id, const std::optional<std::string> &state,
						 const std::optional<std::string> &severity, const std::optional<std::string> &description,
						 const std::optional<std::string> &proposed_fix, const std::optional<std::string> &summary)
{
	std::unique_lock lock(mutex_);
	for (auto &item : items_) {
		if (item.id == id) {
			if (state)
				item.state = *state;
			if (severity)
				item.severity = *severity;
			if (description)
				item.description = *description;
			if (proposed_fix)
				item.proposed_fix = *proposed_fix;
			if (summary)
				item.summary = *summary;
			save_project_unlocked();
			return true;
		}
	}
	return false;
}

bool codereview_manager::confirm_code_review_item(int id)
{
	std::unique_lock lock(mutex_);
	for (auto &item : items_) {
		if (item.id == id) {
			if (item.state == "new") {
				item.state = "confirmed";
			} else if (item.state == "resolved") {
				item.state = "verified-fixed";
			} else if (item.state == "confirmed" || item.state == "verified-fixed") {
				return true;
			} else {
				return false;
			}
			save_project_unlocked();
			return true;
		}
	}
	return false;
}

bool codereview_manager::resolve_code_review_item(int id, const std::string &commit_hash)
{
	std::unique_lock lock(mutex_);
	for (auto &item : items_) {
		if (item.id == id) {
			item.state = "resolved";
			item.resolved_in_commit = commit_hash;
			save_project_unlocked();
			return true;
		}
	}
	return false;
}

std::optional<review_item> codereview_manager::get_code_review_item(int id) const
{
	std::shared_lock lock(mutex_);
	for (const auto &item : items_) {
		if (item.id == id) {
			return item;
		}
	}
	return std::nullopt;
}

static int severity_to_int(const std::string &sev)
{
	if (sev == "nit") return 0;
	if (sev == "low") return 1;
	if (sev == "medium") return 2;
	if (sev == "high") return 3;
	if (sev == "critical") return 4;
	return -1;
}

std::vector<review_item> codereview_manager::list_code_review_items(const std::string &filename_filter, const std::string &severity_filter,
								    bool include_resolved) const
{
	std::shared_lock lock(mutex_);
	std::vector<review_item> res;
	for (const auto &item : items_) {
		if (!include_resolved) {
			if (item.state == "resolved" || item.state == "verified-fixed") {
				continue;
			}
		}
		if (!filename_filter.empty() && !item.filename.starts_with(filename_filter)) {
			continue;
		}
		if (!severity_filter.empty()) {
			int filter_val = severity_to_int(severity_filter);
			int item_val = severity_to_int(item.severity);
			if (filter_val != -1 && item_val != -1) {
				if (item_val < filter_val) {
					continue;
				}
			} else {
				if (item.severity != severity_filter) {
					continue;
				}
			}
		}
		res.push_back(item);
	}
	return res;
}

bool codereview_manager::has_active_items() const
{
	std::shared_lock lock(mutex_);
	for (const auto &item : items_) {
		if (item.state != "resolved" && item.state != "verified-fixed") {
			return true;
		}
	}
	return false;
}

void codereview_manager::clear_all()
{
	std::unique_lock lock(mutex_);
	items_.clear();
	next_id_ = 1;
	save_project_unlocked();
}

static bool file_matches(std::string_view proj_root, std::string_view filename1, std::string_view filename2)
{
	if (filename1.empty() || filename2.empty())
		return false;
	if (filename1 == filename2)
		return true;
	std::filesystem::path p1(filename1);
	std::filesystem::path p2(filename2);
	std::filesystem::path abs1 = p1.is_absolute() ? p1 : std::filesystem::path(proj_root) / p1;
	std::filesystem::path abs2 = p2.is_absolute() ? p2 : std::filesystem::path(proj_root) / p2;
	return abs1.lexically_normal() == abs2.lexically_normal();
}

void codereview_manager::on_line_inserted(const std::string &filename, int y_zero_based)
{
	std::unique_lock lock(mutex_);
	int y_one_based = y_zero_based + 1;
	for (auto &item : items_) {
		if (file_matches(project_root_path_, item.filename, filename) && item.line_number >= y_one_based && item.state != "stale") {
			item.line_number++;
		}
	}
}

void codereview_manager::on_line_deleted(const std::string &filename, int y_zero_based)
{
	std::unique_lock lock(mutex_);
	int y_one_based = y_zero_based + 1;
	for (auto &item : items_) {
		if (file_matches(project_root_path_, item.filename, filename) && item.state != "stale") {
			if (item.line_number == y_one_based) {
				item.state = "stale";
			} else if (item.line_number > y_one_based) {
				item.line_number--;
			}
		}
	}
}

static bool contents_match(const std::string &orig, const std::string &curr)
{
	auto trim = [](const std::string &s) {
		size_t first = s.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return std::string("");
		size_t last = s.find_last_not_of(" \t\r\n");
		return s.substr(first, last - first + 1);
	};
	std::string o = trim(orig);
	std::string c = trim(curr);
	if (o.empty() || c.empty()) return false;
	if (o == c) return true;
	if (o.length() >= 5 && c.find(o) != std::string::npos) return true;
	if (c.length() >= 5 && o.find(c) != std::string::npos) return true;
	return false;
}

void codereview_manager::verify_line_contents(std::string_view filename, std::span<const std::string> lines)
{
	std::unique_lock lock(mutex_);
	bool changed = false;
	for (auto &item : items_) {
		if (file_matches(project_root_path_, item.filename, filename) && item.state != "stale" && item.line_number > 0) {
			int idx = item.line_number - 1;
			if (idx >= 0 && idx < (int)lines.size()) {
				if (!contents_match(item.line_content, lines[idx])) {
					item.state = "stale";
					changed = true;
				}
			} else {
				item.state = "stale";
				changed = true;
			}
		}
	}
	if (changed) {
		save_project_unlocked();
	}
}
