#include "fs_replace_content.h"
#include <algorithm>
#include <format>
#include <dtl/dtl.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../agentlib/interactions/base.h"
#include "../../markdown_utils.h"

namespace tools {

class interaction_fs_replace_content : public agentlib::agent_interaction {
public:
    explicit interaction_fs_replace_content(const std::string& path) {
        call_text_ = "Applying text replacement to " + path;
    }

    agentlib::interaction_type get_type() const override { return agentlib::interaction_type::action; }
    agentlib::interaction_role get_role() const override { return agentlib::interaction_role::agent; }

    bool needs_subpanel_header() const override { return true; }
    std::string get_subpanel_label() const override { return "Applying replacement"; }

    void set_result(const std::string& res) {
        result_text_ = res;
        invalidate_cache();
    }

    void set_target_type(const std::string& path, bool is_buffer) {
        (void)path;
        (void)is_buffer;
    }

    void set_diff(const std::vector<std::string>& before, const std::vector<std::string>& after) {
        dtl::Diff<std::string, std::vector<std::string>> d(before, after);
        d.compose();
        d.composeUnifiedHunks();

        std::stringstream ss;
        d.printUnifiedFormat(ss);

        std::string line;
        diff_lines_.clear();
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            diff_lines_.push_back(line);
        }
        invalidate_cache();
    }

    std::string get_raw_text() const override {
        std::string raw = call_text_;
        if (!result_text_.empty()) {
            raw += "\nResult: " + result_text_;
        }
        for (const auto& dl : diff_lines_) {
            raw += "\n" + dl;
        }
        return raw;
    }

protected:
    std::vector<agentlib::interaction_line> format_lines(int width, agentlib::background_mode bg) const override {
        int label_color = get_color_pair(agentlib::interaction_role::thinking, bg);
        auto lines = wrap_text("", call_text_, width, label_color);

        if (!diff_lines_.empty()) {
            lines.push_back({std::string(std::min(width, 20), '-'), label_color});

            for (const auto& dl : diff_lines_) {
                int color = 3; // Default Yellow on Dark Blue
                if (dl.empty()) {
                    lines.push_back({std::string(width, ' '), color});
                    continue;
                }

                if (dl[0] == '-')
                    color = 31; // Bright Red on Dark Blue
                else if (dl[0] == '+')
                    color = 30; // Bright Green on Dark Blue
                else if (dl.length() > 2 && dl[0] == '@' && dl[1] == '@')
                    color = 32; // Bright Cyan on Dark Blue

                auto dl_wrapped = wrap_text("", dl, width, color);
                lines.insert(lines.end(), dl_wrapped.begin(), dl_wrapped.end());
            }
        }

        if (!result_text_.empty()) {
            int res_color = get_color_pair(agentlib::interaction_role::agent, bg);
            if (result_text_.find("Successfully") != 0) {
                res_color = get_color_pair(agentlib::interaction_role::error, bg);
            }
            lines.push_back({"", res_color});
            auto res_lines = wrap_text("", "-> " + result_text_, width, res_color);
            lines.insert(lines.end(), res_lines.begin(), res_lines.end());
        }

        for (auto& line : lines) {
            int len = markdown_utils::display_width(line.text);
            if (len < width) {
                line.text += std::string(width - len, ' ');
            }
        }

        return lines;
    }

private:
    std::string call_text_;
    std::string result_text_;
    std::vector<std::string> diff_lines_;
};

static std::vector<std::string> split_lines(const std::string& str) {
    std::vector<std::string> res;
    std::stringstream ss(str);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        res.push_back(line);
    }
    return res;
}

fs_replace_content_tool::fs_replace_content_tool(fs_replace_content_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<interaction_fs_replace_content>(args_.path);
}

std::shared_ptr<agentlib::agent_interaction> fs_replace_content_tool::get_interaction() const {
    return interaction_;
}

bool fs_replace_content_tool::validate_runtime(const agentlib::tool_context& /*ctx*/, std::string& out_error) const {
    if (!std::filesystem::exists(args_.safe_path)) {
        out_error = "Error: File does not exist. fs_replace_content can only edit existing files.";
        return false;
    }
    return true;
}

std::string fs_replace_content_tool::execute(agentlib::tool_context& ctx) {
    std::string result_msg = execute_disk_fallback(ctx);

    if (result_msg.starts_with("Error")) {
        if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_replace_content>(interaction_)) {
            custom_interaction->set_result(result_msg);
            if (ctx.trigger_ui_update) {
                ctx.trigger_ui_update();
            }
        }
        return result_msg;
    }

    return result_msg;
}

std::string fs_replace_content_tool::execute_disk_fallback(agentlib::tool_context& ctx) {
    // 1. Read file into string
    std::ifstream in(args_.safe_path, std::ios::binary);
    if (!in.is_open()) {
        return "Error: Could not open file for reading during execution.";
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string file_content = buffer.str();
    in.close();

    // 2. Find matches
    std::vector<size_t> match_indices;
    size_t pos = file_content.find(args_.target_content, 0);
    while (pos != std::string::npos) {
        match_indices.push_back(pos);
        pos = file_content.find(args_.target_content, pos + args_.target_content.length());
    }

    if (match_indices.empty()) {
        return "Error: target_content not found in the file. Check spelling and formatting.";
    }

    // 3. Resolve starting line numbers for matches
    std::vector<int> match_lines;
    for (size_t idx : match_indices) {
        int line_num = 1;
        for (size_t c_idx = 0; c_idx < idx; ++c_idx) {
            if (file_content[c_idx] == '\n') {
                line_num++;
            }
        }
        match_lines.push_back(line_num);
    }

    size_t chosen_idx_pos = 0;

    // 4. Disambiguate if multiple matches found
    if (match_lines.size() > 1) {
        if (args_.line_hint.has_value()) {
            int hint = args_.line_hint.value();
            size_t best_match = 0;
            int min_diff = std::abs(match_lines[0] - hint);
            for (size_t m = 1; m < match_lines.size(); ++m) {
                int diff = std::abs(match_lines[m] - hint);
                if (diff < min_diff) {
                    min_diff = diff;
                    best_match = m;
                }
            }
            chosen_idx_pos = best_match;
        } else {
            std::stringstream err_ss;
            err_ss << "Error: Multiple matches (" << match_lines.size() << ") found for target_content at line numbers: [";
            for (size_t i = 0; i < match_lines.size(); ++i) {
                err_ss << match_lines[i] << (i + 1 < match_lines.size() ? ", " : "");
            }
            err_ss << "]. Please pass the optional 'line_hint' parameter to specify which occurrence to edit.";
            return err_ss.str();
        }
    }

    size_t replace_pos = match_indices[chosen_idx_pos];
    int start_line = match_lines[chosen_idx_pos];

    // 5. Construct substituted content
    std::string new_content = file_content.substr(0, replace_pos) + 
                              args_.replacement_content + 
                              file_content.substr(replace_pos + args_.target_content.length());

    // 6. Generate diff
    std::vector<std::string> before_lines = split_lines(file_content);
    std::vector<std::string> after_lines = split_lines(new_content);

    // 7. Write substituted content back to disk
    std::ofstream out(args_.safe_path, std::ios::binary);
    if (!out.is_open()) {
        return "Error: Could not open file for writing during execution.";
    }
    out.write(new_content.data(), new_content.length());
    out.close();

    // 8. Sync with live editor buffer if open
    bool is_buffer = false;
    if (ctx.doc_provider && ctx.doc_provider->get_open_document(args_.safe_path)) {
        is_buffer = true;

        nlohmann::json edits_json = nlohmann::json::array();
        nlohmann::json edit_json;
        edit_json["line_number"] = start_line;
        edit_json["type"] = "replace";
        edit_json["original_text"] = args_.target_content;
        edit_json["replace_with"] = args_.replacement_content;
        edits_json.push_back(edit_json);

        ctx.doc_provider->apply_live_edits(args_.safe_path, edits_json.dump());
    }

    // 9. Update UI and return status
    std::string result_msg = std::format("Successfully replaced target_content in {} starting at line {}.", args_.path, start_line);
    if (auto custom_interaction = std::dynamic_pointer_cast<interaction_fs_replace_content>(interaction_)) {
        custom_interaction->set_target_type(args_.path, is_buffer);
        custom_interaction->set_diff(before_lines, after_lines);
        custom_interaction->set_result(result_msg);
        if (ctx.trigger_ui_update) {
            ctx.trigger_ui_update();
        }
    }

    return result_msg;
}

} // namespace tools
