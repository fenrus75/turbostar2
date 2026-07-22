#include "agentlib/command_registry.h"
#include "agentlib/ai_agent.h"
#include "agentlib/ai_model.h"
#include "agentlib/skill_manager.h"
#include "event_queue.h"
#include "event_logger.h"
#include "fs_utils.h"
#include <algorithm>
#include <format>

using namespace agentlib;

namespace {

class quit_command : public agent_command
{
      public:
	std::string get_name() const override { return "quit"; }
	std::string get_description() const override { return "Close the agent window"; }
	void execute(const context &ctx) override
	{
		editor_event ev;
		ev.type = event_type::close_window;
		ev.key_code = ctx.window_id;
		ctx.global_queue->push(ev);
	}
};

class model_command : public agent_command
{
      public:
	std::string get_name() const override { return "model"; }
	std::string get_description() const override { return "Switch the AI model for this agent"; }
	void execute(const context &ctx) override
	{
		editor_event ev;
		ev.type = event_type::agent_switch_model;
		ev.key_code = ctx.window_id;
		ctx.global_queue->push(ev);
	}
};

class mcp_command : public agent_command
{
      public:
	std::string get_name() const override { return "mcp"; }
	std::string get_description() const override { return "Open the MCP Servers dialog"; }
	void execute(const context &ctx) override
	{
		editor_event ev;
		ev.type = event_type::mcp_config;
		ctx.global_queue->push(ev);
	}
};

class skills_command : public agent_command
{
      public:
	std::string get_name() const override { return "skills"; }
	std::string get_description() const override { return "List all available agent skills"; }
	void execute(const context &ctx) override
	{
		auto &skills = skill_manager::get_instance().get_skills();
		std::string skills_text = "Available Skills:\n";
		int visible_count = 0;
		for (const auto &s : skills) {
			if (s.visible) {
				skills_text += std::format("- {} ({})\n", s.name, s.description);
				visible_count++;
			}
		}
		if (visible_count == 0) {
			skills_text += "  (No skills available)";
		}
		ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(skills_text));
	}
};

class compact_command : public agent_command
{
      public:
	std::string get_name() const override { return "compact"; }
	std::string get_description() const override { return "Force stateful response compaction manually"; }
	void execute(const context &ctx) override
	{
		ctx.agent->force_compaction();
	}
};

class save_command : public agent_command
{
      public:
	std::string get_name() const override { return "save"; }
	std::string get_description() const override { return "Save the active context to disk manually"; }
	void execute(const context &ctx) override
	{
		std::string filepath = ctx.arguments;
		if (filepath.empty()) {
			std::string tmp_dir = fs_utils::get_project_tmp_dir();
			filepath = tmp_dir + "/agent_chat_" + std::to_string(ctx.window_id) + ".json";
		}
		ctx.agent->save_conversation(filepath);
		event_logger::get_instance().log("Conversation saved to: {}", filepath);
		ctx.agent->add_interaction(
		    std::make_shared<agentlib::interaction_system_message>(std::format("Conversation saved to: {}", filepath)));
	}
};

class info_command : public agent_command
{
      public:
	std::string get_name() const override { return "info"; }
	std::string get_description() const override { return "Show current model configuration and info"; }
	void execute(const context &ctx) override
	{
		std::string info_str = "Agent Info:\n";
		if (ctx.agent->get_model()) {
			auto m = ctx.agent->get_model();
			info_str += std::format("  Active Model ID:   {}\n", m->get_id());
			info_str += std::format("  Active Model Name: {}\n", m->get_name());
			info_str += std::format("  Endpoint URL:      {}\n", m->get_url());
			info_str += std::format("  API Type:          {}\n", (m->get_api_type() == api_type::openai ? "openai" :
										m->get_api_type() == api_type::openai_response ? "openai_response" :
										m->get_api_type() == api_type::gemini ? "gemini" :
										m->get_api_type() == api_type::claude ? "claude" :
										m->get_api_type() == api_type::copilot ? "copilot" : "unknown"));
		} else {
			info_str += "  (No active model configured on agent)\n";
		}
		ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(info_str));
	}
};

class stats_command : public agent_command
{
      public:
	std::string get_name() const override { return "stats"; }
	std::string get_description() const override { return "Show compaction and performance statistics"; }
	void execute(const context &ctx) override
	{
		auto stats = ctx.agent->get_stats();
		std::string stats_str = "Agent Statistics:\n";
		if (stats.empty()) {
			stats_str += "  (No stats recorded yet)";
		} else {
			for (const auto &[key, value] : stats) {
				stats_str += "  " + key + ": " + std::to_string(value) + "\n";
			}
		}
		ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(stats_str));
	}
};

class memory_command : public agent_command
{
      public:
	std::string get_name() const override { return "memory"; }
	std::string get_description() const override { return "List all paged-out history archives"; }
	void execute(const context &ctx) override
	{
		std::string mem_index = ctx.agent->get_memory_index();
		ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(mem_index));
	}
};

class episode_command : public agent_command
{
      public:
	std::string get_name() const override { return "episode"; }
	std::string get_description() const override { return "Drop a semantic anchor and compress history manually"; }
	void execute(const context &ctx) override
	{
		std::string title = ctx.arguments.empty() ? "User Episode" : ctx.arguments;
		size_t start_index = 1;
		auto convo = ctx.agent->get_conversation();
		for (int i = static_cast<int>(convo.size()) - 1; i >= 0; --i) {
			if (convo[i].role == "system" && convo[i].content.find("Episode Archived") != std::string::npos) {
				start_index = i + 1;
				break;
			}
		}
		ctx.agent->page_out_context(start_index, convo.size(), title, "User manually triggered episode: " + title,
					 {"manual-episode"});
		ctx.agent->add_interaction(
		    std::make_shared<agentlib::interaction_system_message>("Episode manually recorded. History compressed."));
	}
};

class pageout_command : public agent_command
{
      public:
	std::string get_name() const override { return "pageout"; }
	std::string get_description() const override { return "Page out turns or a specific active episode"; }
	void execute(const context &ctx) override
	{
		std::string arg = ctx.arguments;
		if (arg.starts_with("episode_")) {
			if (ctx.agent->set_episode_state(arg, 99)) {
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(
				    "Successfully paged out episode: " + arg));
			} else {
				ctx.agent->add_interaction(
				    std::make_shared<agentlib::interaction_system_message>("Failed to page out episode: " + arg));
			}
		} else {
			try {
				int n = std::stoi(arg);
				ctx.agent->page_out_context(1, n + 1, "Manual Pageout",
							 "User manually triggered /pageout " + std::to_string(n), {});
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(
				    "Successfully paged out " + std::to_string(n) + " turns."));
			} catch (...) {
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(
				    "Usage: /pageout <number_of_turns> or /pageout <episode_id>"));
			}
		}
	}
};

class pagein_command : public agent_command
{
      public:
	std::string get_name() const override { return "pagein"; }
	std::string get_description() const override { return "Restore or change compression level of an episode"; }
	void execute(const context &ctx) override
	{
		if (ctx.arguments.empty()) {
			std::vector<std::string> paged_in = ctx.agent->page_in_history_auto(1);
			if (paged_in.empty()) {
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(
				    "No episodes paged in (either 50% limit reached or all episodes already active)."));
			} else {
				std::string msg = "Successfully paged in episodes: ";
				for (size_t i = 0; i < paged_in.size(); ++i) {
					msg += paged_in[i] + (i < paged_in.size() - 1 ? ", " : "");
				}
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(msg));
			}
		} else {
			std::string args = ctx.arguments;
			std::string episode_id = args;
			int level = 1;

			size_t space_pos = args.find(' ');
			if (space_pos != std::string::npos) {
				episode_id = args.substr(0, space_pos);
				try {
					level = std::stoi(args.substr(space_pos + 1));
				} catch (...) {
					level = 1;
				}
			}

			if (ctx.agent->set_episode_state(episode_id, level)) {
				ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(
				    "Successfully paged in " + episode_id + " at level " + std::to_string(level)));
			} else {
				ctx.agent->add_interaction(
				    std::make_shared<agentlib::interaction_system_message>("Failed to page in " + episode_id));
			}
		}
	}
};

class help_command : public agent_command
{
      public:
	std::string get_name() const override { return "help"; }
	std::string get_description() const override { return "Show this help message"; }
	void execute(const context &ctx) override
	{
		std::string help_str = "Available Commands:\n";
		auto &reg = command_registry::get_instance();
		for (const auto &name : reg.get_command_names()) {
			if (auto cmd = reg.get_command(name)) {
				std::string padded = name;
				if (padded.length() < 16) {
					padded.append(16 - padded.length(), ' ');
				}
				help_str += std::format("  /{} - {}\n", padded, cmd->get_description());
			}
		}
		if (!help_str.empty() && help_str.back() == '\n') {
			help_str.pop_back();
		}
		ctx.agent->add_interaction(std::make_shared<agentlib::interaction_system_message>(help_str));
	}
};

class clear_command : public agent_command
{
      public:
	std::string get_name() const override { return "clear"; }
	std::string get_description() const override { return "Clear conversation history and start a fresh context"; }
	void execute(const context &ctx) override
	{
		if (ctx.agent) {
			ctx.agent->clear_conversation();
		}
	}
};

} // namespace

command_registry &command_registry::get_instance()
{
	static command_registry instance;
	return instance;
}

command_registry::command_registry()
{
	register_command(std::make_unique<help_command>());
	register_command(std::make_unique<quit_command>());
	register_command(std::make_unique<model_command>());
	register_command(std::make_unique<mcp_command>());
	register_command(std::make_unique<skills_command>());
	register_command(std::make_unique<compact_command>());
	register_command(std::make_unique<save_command>());
	register_command(std::make_unique<info_command>());
	register_command(std::make_unique<stats_command>());
	register_command(std::make_unique<memory_command>());
	register_command(std::make_unique<episode_command>());
	register_command(std::make_unique<pageout_command>());
	register_command(std::make_unique<pagein_command>());
	register_command(std::make_unique<clear_command>());
}

void command_registry::register_command(std::unique_ptr<agent_command> cmd)
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::string name = cmd->get_name();
	commands_[name] = std::move(cmd);
	if (std::find(command_names_.begin(), command_names_.end(), name) == command_names_.end()) {
		command_names_.push_back(name);
		std::sort(command_names_.begin(), command_names_.end());
	}
}

void command_registry::unregister_command(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	commands_.erase(name);
	auto it = std::find(command_names_.begin(), command_names_.end(), name);
	if (it != command_names_.end()) {
		command_names_.erase(it);
	}
}

agent_command *command_registry::get_command(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = commands_.find(name);
	if (it != commands_.end()) {
		return it->second.get();
	}
	return nullptr;
}

std::vector<std::string> command_registry::get_command_names()
{
	std::lock_guard<std::mutex> lock(mutex_);
	return command_names_;
}
