#include "image_tool.h"

namespace agentlib {

interaction_image_tool::interaction_image_tool(std::string tool_name, std::string call_text, std::string src_uri)
    : tool_name_(std::move(tool_name)), call_text_(std::move(call_text)), src_uri_(std::move(src_uri)) {}

std::string interaction_image_tool::get_raw_text() const {
    std::string res = "Tool Call: " + call_text_;
    if (!result_text_.empty()) {
        res += "\nTool Result: " + result_text_;
    }
    return res;
}

void interaction_image_tool::set_result(std::string result_text) {
    result_text_ = std::move(result_text);
    invalidate_cache();
}

void interaction_image_tool::set_output_image(std::string dst_uri) {
    dst_uri_ = std::move(dst_uri);
    invalidate_cache();
}

std::vector<interaction_line> interaction_image_tool::format_lines(int width, background_mode bg) const {
	std::vector<interaction_line> lines;
	int call_color = get_color_pair(interaction_role::thinking, bg);
	int result_color = get_color_pair(interaction_role::agent, bg);

	// 1. Tool Call Text
	auto call_lines = wrap_text("* Executing tool: ", call_text_, width, call_color);
	lines.insert(lines.end(), call_lines.begin(), call_lines.end());

	// 2. Source Image Thumbnail Placeholder
	if (!src_uri_.empty()) {
		interaction_line space;
		space.text = "";
		space.color_pair = call_color;
		lines.push_back(space);

		for (int r = 0; r < 5; ++r) {
			interaction_line thumb_line;
			thumb_line.text = "__THUMBNAIL__:" + std::to_string(r) + ":" + src_uri_;
			thumb_line.color_pair = call_color;
			lines.push_back(thumb_line);
		}
	}

	// 3. Tool Result Text
	if (!result_text_.empty()) {
		interaction_line space;
		space.text = "";
		space.color_pair = result_color;
		lines.push_back(space);

		auto res_lines = wrap_text("  ↳ Result: ", result_text_, width, result_color);
		lines.insert(lines.end(), res_lines.begin(), res_lines.end());

		// 4. Destination Image Thumbnail Placeholder
		if (!dst_uri_.empty()) {
			interaction_line result_space;
			result_space.text = "";
			result_space.color_pair = result_color;
			lines.push_back(result_space);

			for (int r = 0; r < 5; ++r) {
				interaction_line thumb_line;
				thumb_line.text = "__THUMBNAIL__:" + std::to_string(r) + ":" + dst_uri_;
				thumb_line.color_pair = result_color;
				lines.push_back(thumb_line);
			}
		}
	}

	return lines;
}

} // namespace agentlib
