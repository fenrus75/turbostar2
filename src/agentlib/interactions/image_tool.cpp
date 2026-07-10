#include "image_tool.h"
#include "../../markdown_utils.h"

namespace agentlib {

static std::string parse_image_uri_from_tool_call(const std::string &tool_name, const std::string &text) {
	if (tool_name.find("image_") == std::string::npos)
		return "";

	size_t img_pos = text.find("images://");
	if (img_pos != std::string::npos) {
		size_t end_pos = img_pos;
		while (end_pos < text.size()) {
			char c = text[end_pos];
			if (c == '"' || c == '\'' || c == ',' || c == ' ' || c == '}' || c == ')' || c == ']') {
				break;
			}
			end_pos++;
		}
		return text.substr(img_pos, end_pos - img_pos);
	}

	for (const auto &ext : {".png", ".jpg", ".jpeg", ".bmp", ".gif"}) {
		size_t ext_pos = text.find(ext);
		if (ext_pos != std::string::npos) {
			size_t start_pos = ext_pos;
			while (start_pos > 0) {
				char c = text[start_pos - 1];
				if (c == '"' || c == '\'' || c == ' ' || c == ',' || c == '{' || c == '(' || c == '[') {
					break;
				}
				start_pos--;
			}
			return text.substr(start_pos, ext_pos + std::string(ext).length() - start_pos);
		}
	}

	return "";
}

interaction_image_tool::interaction_image_tool(std::string tool_name, std::string call_text)
    : tool_name_(std::move(tool_name)), call_text_(std::move(call_text)) {}

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

std::vector<interaction_line> interaction_image_tool::format_lines(int width, background_mode bg) const {
	std::vector<interaction_line> lines;
	int call_color = get_color_pair(interaction_role::thinking, bg);
	int result_color = get_color_pair(interaction_role::agent, bg);

	// 1. Tool Call Text
	auto call_lines = wrap_text("* Executing tool: ", call_text_, width, call_color);
	lines.insert(lines.end(), call_lines.begin(), call_lines.end());

	// 2. Source Image Thumbnail Placeholder
	std::string src_uri = parse_image_uri_from_tool_call(tool_name_, call_text_);
	if (!src_uri.empty()) {
		interaction_line space;
		space.text = "";
		space.color_pair = call_color;
		lines.push_back(space);

		for (int r = 0; r < 5; ++r) {
			interaction_line thumb_line;
			thumb_line.text = "__THUMBNAIL__:" + std::to_string(r) + ":" + src_uri;
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
		size_t uri_pos = result_text_.find("New URI: ");
		if (uri_pos != std::string::npos) {
			std::string dst_uri = result_text_.substr(uri_pos + 9);
			while (!dst_uri.empty() && (dst_uri.back() == '.' || dst_uri.back() == ' ' || dst_uri.back() == '\r' || dst_uri.back() == '\n')) {
				dst_uri.pop_back();
			}
			if (!dst_uri.empty()) {
				interaction_line space;
				space.text = "";
				space.color_pair = result_color;
				lines.push_back(space);

				for (int r = 0; r < 5; ++r) {
					interaction_line thumb_line;
					thumb_line.text = "__THUMBNAIL__:" + std::to_string(r) + ":" + dst_uri;
					thumb_line.color_pair = result_color;
					lines.push_back(thumb_line);
				}
			}
		}
	}

	return lines;
}

} // namespace agentlib
