#pragma once

#include <chrono>
#include <memory>
#include <string>
#include "agentlib/ai_agent.h"
#include "ui/ui_element.h"

class ui_durmovie;

class ui_agent_tile : public ui_container
{
      public:
	ui_agent_tile(std::string name, int x, int y, std::shared_ptr<agentlib::ai_agent> agent);
	~ui_agent_tile() override = default;

	void draw(int abs_x, int abs_y) const override;
	bool update_animation() override;

	int natural_width() const override
	{
		return 20;
	}
	int natural_height() const override
	{
		return 10;
	}

      private:
	std::shared_ptr<agentlib::ai_agent> agent_;
	ui_durmovie *durmovie_{nullptr};

	uint64_t last_tokens_rx_{0};
	std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
	std::chrono::time_point<std::chrono::steady_clock> last_token_increase_time_;
	int current_anim_frame_{0};
	int spinner_frame_{0};
};
