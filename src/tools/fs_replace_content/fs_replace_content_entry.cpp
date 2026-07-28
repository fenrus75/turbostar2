#include "fs_replace_content.h"
#include <algorithm>
#include <format>
#include <dtl/dtl.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "../../agentlib/interactions/base.h"
#include "../../markdown_utils.h"
#include "../../project_manager.h"

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
    if (!str.empty() && (str.back() == '\n' || str.back() == '\r')) {
        // preserve line count matching
    }
    return res;
}

static bool find_symbol_range(const std::vector<lsp_manager::symbol_node>& nodes, std::string_view hint, int& out_start, int& out_end) {
    for (const auto& node : nodes) {
        if (node.name.find(hint) != std::string::npos || std::string_view(node.name) == hint) {
            out_start = node.range.start_y + 1;
            out_end = node.range.end_y + 1;
            return true;
        }
        if (find_symbol_range(node.children, hint, out_start, out_end)) {
            return true;
        }
    }
    return false;
}

static bool fallback_find_symbol_range(const std::vector<std::string>& file_lines, std::string_view hint, int& out_start, int& out_end) {
    if (hint.empty()) return false;
    for (size_t i = 0; i < file_lines.size(); ++i) {
        const auto& line = file_lines[i];
        if (line.find(hint) != std::string::npos) {
            if (line.find('(') != std::string::npos || line.find('{') != std::string::npos ||
                line.find("def ") != std::string::npos || line.find("class ") != std::string::npos ||
                line.find("fn ") != std::string::npos || line.find("struct ") != std::string::npos) {
                out_start = static_cast<int>(i + 1);
                int brace_count = 0;
                bool found_brace = false;
                out_end = std::min(static_cast<int>(file_lines.size()), out_start + 120);
                for (size_t j = i; j < file_lines.size(); ++j) {
                    for (char c : file_lines[j]) {
                        if (c == '{') {
                            brace_count++;
                            found_brace = true;
                        } else if (c == '}') {
                            brace_count--;
                            if (found_brace && brace_count <= 0) {
                                out_end = static_cast<int>(j + 1);
                                return true;
                            }
                        }
                    }
                }
                return true;
            }
        }
    }
    return false;
}

static std::string normalize_line_for_relaxed(std::string_view line, bool strip_leading) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
        line.remove_suffix(1);
    }
    if (strip_leading) {
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        return std::string(line);
    }
    std::string res;
    res.reserve(line.size() * 2);
    for (char c : line) {
        if (c == '\t') {
            res.append("    ");
        } else {
            res.push_back(c);
        }
    }
    return res;
}

static void get_line_byte_range(const std::string& text, int start_line_1based, int num_lines, bool target_ends_with_newline, size_t& out_start, size_t& out_len) {
    int current_line = 1;
    size_t idx = 0;
    size_t n = text.size();
    while (idx < n && current_line < start_line_1based) {
        if (text[idx] == '\n') {
            current_line++;
        }
        idx++;
    }
    out_start = idx;
    int lines_counted = 0;
    while (idx < n && lines_counted < num_lines) {
        if (text[idx] == '\n') {
            lines_counted++;
            if (lines_counted == num_lines && !target_ends_with_newline) {
                break;
            }
        }
        idx++;
    }
    out_len = idx - out_start;
}

fs_replace_content_tool::fs_replace_content_tool(fs_replace_content_args args) : args_(std::move(args)) {
    interaction_ = std::make_shared<interaction_fs_replace_content>(args_.path);
}

std::shared_ptr<agentlib::agent_interaction> fs_replace_content_tool::get_interaction() const {
    return interaction_;
}

bool fs_replace_content_tool::validate_runtime(const agentlib::tool_context& ctx, std::string& out_error) const {
    std::string path_to_use = args_.safe_path;
    auto* vfs = ctx.fs_security.get_vfs();
    if (vfs && vfs->is_local_path_available(args_.safe_path)) {
        path_to_use = vfs->get_local_path(args_.safe_path);
    }
    if (!std::filesystem::exists(path_to_use)) {
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

static std::string format_multiple_matches_error(
    std::string_view path,
    size_t match_count,
    const std::vector<int>& lines,
    bool is_relaxed,
    const std::optional<std::string>& function_hint,
    const std::optional<int>& line_hint)
{
    std::stringstream err_ss;
    std::string match_type = is_relaxed ? "relaxed matches" : "matches";
    
    err_ss << "Error: Multiple " << match_type << " (" << match_count << ") found for target_content in " << path << " at line numbers: [";
    for (size_t i = 0; i < lines.size(); ++i) {
        err_ss << lines[i] << (i + 1 < lines.size() ? ", " : "");
    }
    err_ss << "]. ";

    bool has_func = function_hint.has_value() && !function_hint->empty();
    bool has_line = line_hint.has_value();

    if (!has_func && !has_line) {
        err_ss << "Please pass 'function_hint' parameter with the enclosing function/method name (e.g. 'execute_disk_fallback'), or pass 'line_hint' to specify which occurrence to edit.";
    } else if (has_func && !has_line) {
        err_ss << "Multiple occurrences exist even within function/scope '" << function_hint.value() << "'. Please pass 'line_hint' to specify which line to edit, or include more context lines in target_content.";
    } else if (!has_func && has_line) {
        err_ss << "Multiple occurrences exist near line_hint " << line_hint.value() << ". Please pass 'function_hint' with the enclosing function name to narrow the scope.";
    } else {
        err_ss << "Multiple occurrences exist inside function '" << function_hint.value() << "' near line " << line_hint.value() << ". Please expand target_content to include more unique surrounding lines.";
    }

    return err_ss.str();
}

std::string fs_replace_content_tool::execute_disk_fallback(agentlib::tool_context& ctx) {
    std::string path_to_use = args_.safe_path;
    auto* vfs = ctx.fs_security.get_vfs();
    if (vfs && vfs->is_local_path_available(args_.safe_path)) {
        path_to_use = vfs->get_local_path(args_.safe_path);
    }
    // 1. Read file into string
    std::ifstream in(path_to_use, std::ios::binary);
    if (!in.is_open()) {
        return "Error: Could not open file for reading during execution.";
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string file_content = buffer.str();
    in.close();

    std::vector<std::string> file_lines = split_lines(file_content);

    // Resolve scope using function_hint if provided
    int scope_start = 1;
    int scope_end = std::numeric_limits<int>::max();
    if (args_.function_hint.has_value() && !args_.function_hint->empty()) {
        std::string_view f_hint = args_.function_hint.value();
        auto symbols = project_manager::get_instance().lsp_query_document_symbols(args_.safe_path);
        if (!find_symbol_range(symbols, f_hint, scope_start, scope_end)) {
            fallback_find_symbol_range(file_lines, f_hint, scope_start, scope_end);
        }
    }

    // 2. Find strict exact matches
    std::vector<size_t> match_indices;
    std::vector<int> match_lines;
    size_t pos = file_content.find(args_.target_content, 0);
    while (pos != std::string::npos) {
        int line_num = 1;
        for (size_t c_idx = 0; c_idx < pos; ++c_idx) {
            if (file_content[c_idx] == '\n') {
                line_num++;
            }
        }
        if (line_num >= scope_start && line_num <= scope_end) {
            match_indices.push_back(pos);
            match_lines.push_back(line_num);
        }
        pos = file_content.find(args_.target_content, pos + args_.target_content.length());
    }

    size_t replace_pos = 0;
    int start_line = 1;
    std::string new_content;

    if (!match_indices.empty()) {
        // Disambiguate exact match if multiple found
        size_t chosen_idx_pos = 0;
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
                return format_multiple_matches_error(args_.path, match_lines.size(), match_lines, false, args_.function_hint, args_.line_hint);
            }
        }
        replace_pos = match_indices[chosen_idx_pos];
        start_line = match_lines[chosen_idx_pos];
        new_content = file_content.substr(0, replace_pos) + 
                      args_.replacement_content + 
                      file_content.substr(replace_pos + args_.target_content.length());
    } else {
        // Step 3: Staged Relaxed Fallback Search
        std::vector<std::string> target_lines = split_lines(args_.target_content);
        size_t target_count = target_lines.size();
        if (target_count == 0 || file_lines.size() < target_count) {
            return "Error: target_content not found in the file. Check spelling and formatting.";
        }

        std::vector<int> relaxed_matches;
        for (size_t i = 0; i + target_count <= file_lines.size(); ++i) {
            int line_num = static_cast<int>(i + 1);
            if (line_num < scope_start || line_num > scope_end) {
                continue;
            }

            bool match_level_b = true;
            bool match_level_c = (target_count >= 3);

            for (size_t j = 0; j < target_count; ++j) {
                std::string file_norm_b = normalize_line_for_relaxed(file_lines[i + j], false);
                std::string targ_norm_b = normalize_line_for_relaxed(target_lines[j], false);
                if (file_norm_b != targ_norm_b) {
                    match_level_b = false;
                }

                if (match_level_c) {
                    std::string file_norm_c = normalize_line_for_relaxed(file_lines[i + j], true);
                    std::string targ_norm_c = normalize_line_for_relaxed(target_lines[j], true);
                    if (file_norm_c != targ_norm_c) {
                        match_level_c = false;
                    }
                }
            }

            if (match_level_b || match_level_c) {
                relaxed_matches.push_back(line_num);
            }
        }

        if (relaxed_matches.empty()) {
            if (args_.function_hint.has_value()) {
                return std::format("Error: target_content not found in {} inside function/scope '{}'. Check formatting and indentation.", args_.path, args_.function_hint.value());
            }
            return "Error: target_content not found in the file. Check spelling and formatting.";
        }

        size_t chosen_relaxed = 0;
        if (relaxed_matches.size() > 1) {
            if (args_.line_hint.has_value()) {
                int hint = args_.line_hint.value();
                int min_diff = std::abs(relaxed_matches[0] - hint);
                for (size_t m = 1; m < relaxed_matches.size(); ++m) {
                    int diff = std::abs(relaxed_matches[m] - hint);
                    if (diff < min_diff) {
                        min_diff = diff;
                        chosen_relaxed = m;
                    }
                }
            } else {
                return format_multiple_matches_error(args_.path, relaxed_matches.size(), relaxed_matches, true, args_.function_hint, args_.line_hint);
            }
        }

        start_line = relaxed_matches[chosen_relaxed];
        size_t match_start_byte = 0;
        size_t match_byte_len = 0;
        bool target_ends_with_newline = !args_.target_content.empty() && (args_.target_content.back() == '\n' || args_.target_content.back() == '\r');
        get_line_byte_range(file_content, start_line, static_cast<int>(target_count), target_ends_with_newline, match_start_byte, match_byte_len);

        if (start_line >= 1 && start_line <= static_cast<int>(file_lines.size()) && !target_lines.empty()) {
            size_t file_indent_len = 0;
            while (file_indent_len < file_lines[start_line - 1].size() && (file_lines[start_line - 1][file_indent_len] == ' ' || file_lines[start_line - 1][file_indent_len] == '\t')) {
                file_indent_len++;
            }
            size_t target_indent_len = 0;
            while (target_indent_len < target_lines[0].size() && (target_lines[0][target_indent_len] == ' ' || target_lines[0][target_indent_len] == '\t')) {
                target_indent_len++;
            }
            if (file_indent_len > target_indent_len) {
                size_t diff_indent = file_indent_len - target_indent_len;
                if (match_byte_len >= diff_indent) {
                    match_start_byte += diff_indent;
                    match_byte_len -= diff_indent;
                }
            }
        }

        new_content = file_content.substr(0, match_start_byte) + 
                      args_.replacement_content + 
                      file_content.substr(match_start_byte + match_byte_len);
    }

    // 6. Generate diff
    std::vector<std::string> before_lines = split_lines(file_content);
    std::vector<std::string> after_lines = split_lines(new_content);

    // 7. Write substituted content back to disk
    std::ofstream out(path_to_use, std::ios::binary);
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
