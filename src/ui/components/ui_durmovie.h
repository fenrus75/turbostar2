#pragma once

#include <chrono>
#include <string>
#include <vector>
#include "ui/ui_element.h"

enum class durmovie_state { idle, active };

class ui_durmovie : public ui_element
{
      public:
	ui_durmovie(std::string name, int x, int y, int width, int height);
	ui_durmovie(std::string name, int x, int y, int width, int height, const std::string &json_str);
	~ui_durmovie() override = default;

	void load_json(const std::string &json_str);

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
	struct durmovie_cell {
		std::string glyph = " ";
		uint8_t fg = 7;
		uint8_t bg = 0;
	};

	struct durmovie_frame {
		int frame_number = 0;
		int delay = 0;				       // In milliseconds
		std::vector<std::vector<durmovie_cell>> cells; // Indexed by [y][x]
	};

	durmovie_state state_{durmovie_state::idle};
	size_t idle_frame_{0};
	size_t current_frame_{0};
	float framerate_{8.0f};
	int size_x_{0};
	int size_y_{0};
	std::vector<durmovie_frame> frames_;

	std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
};
