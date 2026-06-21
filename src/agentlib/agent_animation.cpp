#include "agentlib/agent_animation.h"
#include <exception>
#include <format>
#include <nlohmann/json.hpp>
#include "event_logger.h"
#include "utf8.h"

namespace agentlib
{

agent_animation_registry &agent_animation_registry::get_instance()
{
	static agent_animation_registry *instance = new agent_animation_registry();
	return *instance;
}

void agent_animation_registry::register_animation(const std::string &name, std::shared_ptr<const dur_animation_data> data)
{
	std::lock_guard<std::mutex> lock(mutex_);
	animations_[name] = std::move(data);
	event_logger::get_instance().log(std::format("agent_animation_registry: Registered animation '{}'", name));
}

bool agent_animation_registry::register_animation_json(const std::string &name, const std::string &json_str)
{
	try {
		auto root = nlohmann::json::parse(json_str);
		if (!root.contains("DurMovie")) {
			event_logger::get_instance().log(std::format("agent_animation_registry: JSON for '{}' does not contain 'DurMovie' key", name));
			return false;
		}

		const auto &movie = root["DurMovie"];
		auto data = std::make_shared<dur_animation_data>();
		data->framerate = movie.value("framerate", 8.0f);
		data->size_x = movie.value("sizeX", 80);
		data->size_y = movie.value("sizeY", 23);

		if (movie.contains("frames") && movie["frames"].is_array()) {
			for (const auto &j_frame : movie["frames"]) {
				durmovie_frame frame;
				frame.frame_number = j_frame.value("frameNumber", 1);
				frame.delay = j_frame.value("delay", 0);

				// Initialize frame cell grid to match movie bounds
				frame.cells.resize(data->size_y);
				for (int y = 0; y < data->size_y; ++y) {
					frame.cells[y].resize(data->size_x);
				}

				// Safely parse contents (rows of glyphs)
				if (j_frame.contains("contents") && j_frame["contents"].is_array()) {
					const auto &contents = j_frame["contents"];
					for (size_t y = 0; y < contents.size() && y < static_cast<size_t>(data->size_y); ++y) {
						std::string row_str = contents[y].get<std::string>();
						size_t byte_offset = 0;
						std::string glyph;
						int x = 0;
						while (x < data->size_x && utf8::next_character(row_str, byte_offset, glyph)) {
							frame.cells[y][x].glyph = glyph;
							x++;
						}
					}
				}

				// Safely parse colorMap [col][row][fg/bg]
				if (j_frame.contains("colorMap") && j_frame["colorMap"].is_array()) {
					const auto &color_map = j_frame["colorMap"];
					for (size_t x = 0; x < color_map.size() && x < static_cast<size_t>(data->size_x); ++x) {
						const auto &col_colors = color_map[x];
						if (col_colors.is_array()) {
							for (size_t y = 0; y < col_colors.size() && y < static_cast<size_t>(data->size_y); ++y) {
								const auto &pair = col_colors[y];
								if (pair.is_array() && pair.size() >= 2) {
									frame.cells[y][x].fg = pair[0].get<uint8_t>();
									frame.cells[y][x].bg = pair[1].get<uint8_t>();
								}
							}
						}
					}
				}
				data->frames.push_back(std::move(frame));
			}
		}

		register_animation(name, data);
		return true;
	} catch (const std::exception &e) {
		event_logger::get_instance().log(std::format("agent_animation_registry: Failed to parse animation JSON for '{}': {}", name, e.what()));
		return false;
	}
}

void agent_animation_registry::unregister_animation(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	animations_.erase(name);
	event_logger::get_instance().log(std::format("agent_animation_registry: Unregistered animation '{}'", name));
}

std::shared_ptr<const dur_animation_data> agent_animation_registry::get_animation(const std::string &name) const
{
	std::lock_guard<std::mutex> lock(mutex_);
	auto it = animations_.find(name);
	if (it != animations_.end()) {
		return it->second;
	}
	return nullptr;
}

} // namespace agentlib

void register_agent_animation(const std::string &name, const std::string &json_str)
{
	agentlib::agent_animation_registry::get_instance().register_animation_json(name, json_str);
}

void unregister_agent_animation(const std::string &name)
{
	agentlib::agent_animation_registry::get_instance().unregister_animation(name);
}
