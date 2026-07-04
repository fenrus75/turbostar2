#pragma once
#include <string>

class event_queue;
namespace agentlib {
class ai_agent;
}

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
