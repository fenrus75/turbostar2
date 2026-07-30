#pragma once
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include "agentlib/subagent.h"

namespace agentlib
{

class subagent_manager
{
      public:
	static subagent_manager &get_instance();

	void initialize();
	const std::vector<subagent> &get_subagents() const;
	std::vector<subagent> get_a2a_subagents() const;
	std::optional<subagent> find_subagent_by_name(const std::string &name) const;

	bool is_subagent_a2a_exposed(const std::string &name) const;
	void set_subagent_a2a_exposed(const std::string &name, bool exposed);

	// Dynamic registration interfaces for plugins
	void register_subagent(std::string name, std::string text, std::string animation_json = "");
	void unregister_subagent(std::string name);

	// Programmatic A2A Agent Card synthesis and 3-tier resolution
	std::string generate_a2a_card_for_agent(const std::string &name, const std::string &output_card_path = "") const;
	std::string get_a2a_card(const std::string &name);


      private:
	subagent_manager() = default;
	~subagent_manager() = default;

	void load_builtins();
	void load_from_string(const std::string &content, const std::string &origin);
	void scan_directory(const std::filesystem::path &dir);
	std::optional<subagent> parse_subagent_file(const std::filesystem::path &path);

	std::vector<subagent> subagents_;
};

} // namespace agentlib
