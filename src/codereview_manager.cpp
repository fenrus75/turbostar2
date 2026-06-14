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
						 const std::optional<std::string> &proposed_fix)
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
			} else {
				item.state = "confirmed";
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
		if (!filename_filter.empty() && item.filename != filename_filter) {
			continue;
		}
		if (!severity_filter.empty() && item.severity != severity_filter) {
			continue;
		}
		res.push_back(item);
	}
	return res;
}

void codereview_manager::clear_all()
{
	std::unique_lock lock(mutex_);
	items_.clear();
	next_id_ = 1;
	save_project_unlocked();
}
