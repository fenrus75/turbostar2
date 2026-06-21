#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace agentlib
{

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

struct dur_animation_data {
	float framerate{8.0f};
	int size_x{0};
	int size_y{0};
	std::vector<durmovie_frame> frames;
};

class agent_animation_registry
{
      public:
	static agent_animation_registry &get_instance();

	void register_animation(const std::string &name, std::shared_ptr<const dur_animation_data> data);
	bool register_animation_json(const std::string &name, const std::string &json_str);
	void unregister_animation(const std::string &name);
	std::shared_ptr<const dur_animation_data> get_animation(const std::string &name) const;

      private:
	agent_animation_registry() = default;

	/*
	 * mutex_ protects the animations_ map which holds custom durdraw animation models.
	 * Locking Rules:
	 * - Held briefly during animation registration, unregistration, and lookup.
	 * - No nested locks are acquired while holding this mutex.
	 */
	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::shared_ptr<const dur_animation_data>> animations_;
};

} // namespace agentlib

void register_agent_animation(const std::string &name, const std::string &json_str);
void unregister_agent_animation(const std::string &name);
