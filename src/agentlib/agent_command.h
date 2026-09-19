#pragma once
#include <string>

class event_queue;
namespace agentlib {
class ai_agent;
}

/*

# subclasses of agent_command

| subclass            | filename                            |
| ------------------- | ----------------------------------- |
| clear_command       | src/agentlib/command_registry.cpp   |
| compact_command     | src/agentlib/command_registry.cpp   |
| episode_command     | src/agentlib/command_registry.cpp   |
| help_command        | src/agentlib/command_registry.cpp   |
| info_command        | src/agentlib/command_registry.cpp   |
| mcp_command         | src/agentlib/command_registry.cpp   |
| memory_command      | src/agentlib/command_registry.cpp   |
| model_command       | src/agentlib/command_registry.cpp   |
| pagein_command      | src/agentlib/command_registry.cpp   |
| pageout_command     | src/agentlib/command_registry.cpp   |
| quit_command        | src/agentlib/command_registry.cpp   |
| rescan_command      | src/agentlib/command_registry.cpp   |
| save_command        | src/agentlib/command_registry.cpp   |
| skills_command      | src/agentlib/command_registry.cpp   |
| stats_command       | src/agentlib/command_registry.cpp   |
| sysprompt_command   | src/agentlib/command_registry.cpp   |
| yolo_command        | src/agentlib/command_registry.cpp   |
| grill_me_command    | src/plugins/mattpocock/plugin.cpp   |

*/
class agent_command
{
      public:
	virtual ~agent_command() = default;

	virtual std::string get_name() const = 0;
	virtual std::string get_description() const = 0;

	struct context {
		agentlib::ai_agent *agent;
		int window_id;
		std::string arguments;
		event_queue *global_queue;
	};

	virtual void execute(const context &ctx) = 0;
};
