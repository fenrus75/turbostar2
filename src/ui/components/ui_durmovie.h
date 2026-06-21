#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "agentlib/agent_animation.h"
#include "ui/ui_element.h"

enum class durmovie_state { idle, active };

class ui_durmovie : public ui_element
{
      public:
	ui_durmovie(std::string name, int x, int y, int width, int height);
	ui_durmovie(std::string name, int x, int y, int width, int height, const std::string &json_str);
	ui_durmovie(std::string name, int x, int y, int width, int height, std::shared_ptr<const agentlib::dur_animation_data> animation);
	~ui_durmovie() override = default;

	void load_json(const std::string &json_str);

	void set_animation(std::shared_ptr<const agentlib::dur_animation_data> animation);
	std::shared_ptr<const agentlib::dur_animation_data> get_animation() const
	{
		return animation_;
	}

	void draw(int abs_x, int abs_y) const override;
	bool handle_event(const editor_event &ev, int abs_x, int abs_y) override;
	bool update_animation() override;

	void set_state(durmovie_state state);
	durmovie_state get_state() const
	{
		return state_;
	}

	void set_idle_frame(size_t frame_idx)
	{
		idle_frame_ = frame_idx;
	}
	size_t get_idle_frame() const
	{
		return idle_frame_;
	}

      private:
	durmovie_state state_{durmovie_state::idle};
	size_t idle_frame_{0};
	size_t current_frame_{0};
	std::shared_ptr<const agentlib::dur_animation_data> animation_;

	std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
};
