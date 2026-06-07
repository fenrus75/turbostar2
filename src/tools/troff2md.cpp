#include "troff2md.h"
#include <vector>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <set>
#include <unordered_set>
#include <iostream>
#include <cctype>

enum class TPState {
    NORMAL,
    EXPECTING_TAG,
    IN_BODY
};

struct ParserState {
    int fill_depth = 0;
    std::vector<int> indent_stack = {0};
    TPState tp_state = TPState::NORMAL;
    bool in_table = false;
    std::vector<std::string> table_lines;
    std::string current_inline_style = "";

    int current_indent() const {
        return indent_stack.back();
    }
};

static const std::unordered_set<std::string> structural_macros = {
    ".SH", ".SS", ".P", ".PP", ".TP", ".IP", ".TQ", ".RS", ".RE"
};

static std::vector<std::string> split_lines(const std::string& input) {
    std::vector<std::string> lines;
    std::istringstream iss(input);
    std::string line;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }
    return lines;
}

static std::string comment_filter(const std::string& /*content*/, ParserState& /*state*/) {
    return "";
}

static std::string pass_through_filter(const std::string& content, ParserState& /*state*/) {
    return content + '\n';
}

static std::string remove_quotes(const std::string& input) {
    if (input.size() >= 2 && input.front() == '"' && input.back() == '"') {
        return input.substr(1, input.size() - 2);
    }
    return input;
}

static std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    for (char c : input) {
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
        } else if (std::isspace(static_cast<unsigned char>(c)) && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

static std::string alternating_filter(const std::string& content, const std::string& style1, const std::string& style2) {
    std::vector<std::string> tokens = tokenize(content);
    std::string result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::string& style = (i % 2 == 0) ? style1 : style2;
        std::string word = remove_quotes(tokens[i]);
        if (style == "B") {
            result += "**" + word + "**";
        } else if (style == "I") {
            result += "*" + word + "*";
        } else {
            result += word;
        }
    }
    return result + " ";
}

static std::string process_escapes(const std::string& input, ParserState& state) {
    std::string result;
    bool has_newline = (!input.empty() && input.back() == '\n');
    size_t len = has_newline ? input.length() - 1 : input.length();

    bool has_text = false;
    for (size_t i = 0; i < len; ++i) {
        if (!std::isspace(input[i])) {
            has_text = true;
            break;
        }
    }

    if (has_text && !state.current_inline_style.empty()) {
        result += state.current_inline_style;
    }

    for (size_t i = 0; i < len; ++i) {
        if (input[i] == '\\' && i + 1 < input.length()) {
            char next = input[i+1];
            if (next == 'f' && i + 2 < input.length()) {
                char font = input[i+2];
                i += 2;
                if (font == 'B' || font == '3') {
                    if (has_text && !state.current_inline_style.empty()) result += state.current_inline_style;
                    state.current_inline_style = "**";
                    if (has_text) result += "**";
                } else if (font == 'I' || font == '2') {
                    if (has_text && !state.current_inline_style.empty()) result += state.current_inline_style;
                    state.current_inline_style = "*";
                    if (has_text) result += "*";
                } else if (font == 'R' || font == 'P' || font == '1') {
                    if (has_text && !state.current_inline_style.empty()) {
                        result += state.current_inline_style;
                    }
                    state.current_inline_style = "";
                }
            } else if (next == '-') {
                result += "-";
                i++;
            } else if (next == '&') {
                i++;
            } else if (next == '(' && i + 3 < input.length()) {
                std::string spec = input.substr(i+2, 2);
                i += 3;
                if (spec == "aq") result += "'";
                else if (spec == "dq") result += "\"";
                else if (spec == "bu") result += "*";
                else if (spec == "co") result += "©";
                else if (spec == "em") result += "—";
                else if (spec == "en") result += "–";
                else {
                    result += "\\(";
                    result += spec;
                }
            } else if (next == 'e') {
                result += "\\";
                i++;
            } else {
                result += input[i];
            }
        } else {
            result += input[i];
        }
    }
    if (has_text && !state.current_inline_style.empty()) {
        result += state.current_inline_style;
    }
    if (has_newline) {
        result += '\n';
    }
    return result;
}

static std::string ft_filter(const std::string& content, ParserState& state) {
    std::string arg = remove_quotes(content);
    if (arg == "B" || arg == "3") {
        state.current_inline_style = "**";
    } else if (arg == "I" || arg == "2") {
        state.current_inline_style = "*";
    } else {
        state.current_inline_style = "";
    }
    return "";
}

static std::string sh_filter(const std::string& content, ParserState& /*state*/) {
    return "\n# " + remove_quotes(content) + '\n';
}

static std::string ss_filter(const std::string& content, ParserState& /*state*/) {
    return "\n## " + remove_quotes(content) + '\n';
}

static std::string b_filter(const std::string& content, ParserState& /*state*/) {
    return "**" + remove_quotes(content) + "** ";
}

static std::string i_filter(const std::string& content, ParserState& /*state*/) {
    return "*" + remove_quotes(content) + "* ";
}

static std::string br_filter(const std::string& content, ParserState& /*state*/) { return alternating_filter(content, "B", "R"); }
static std::string bi_filter(const std::string& content, ParserState& /*state*/) { return alternating_filter(content, "B", "I"); }
static std::string ir_filter(const std::string& content, ParserState& /*state*/) { return alternating_filter(content, "I", "R"); }
static std::string ri_filter(const std::string& content, ParserState& /*state*/) { return alternating_filter(content, "R", "I"); }
static std::string rb_filter(const std::string& content, ParserState& /*state*/) { return alternating_filter(content, "R", "B"); }
static std::string ib_filter(const std::string& content, ParserState& /*state*/) {
    return alternating_filter(content, "*", "**");
}

static std::string p_filter(const std::string& /*content*/, ParserState& /*state*/) {
    return "\n";
}

static std::string tp_filter(const std::string& /*content*/, ParserState& state) {
    state.tp_state = TPState::EXPECTING_TAG;
    return "\n";
}

static std::string ip_filter(const std::string& content, ParserState& state) {
    state.tp_state = TPState::IN_BODY;
    if (content.empty()) {
        return "\n";
    }
    std::vector<std::string> tokens = tokenize(content);
    std::string tag = tokens.empty() ? "" : remove_quotes(tokens[0]);
    if (tag == "\\(bu") {
        return "* \n";
    }
    return "* " + tag + "\n";
}

static std::string tq_filter(const std::string& /*content*/, ParserState& state) {
    state.tp_state = TPState::EXPECTING_TAG;
    return "";
}

static std::string nf_filter(const std::string& /*content*/, ParserState& state) {
    if (state.fill_depth++ == 0) {
        return "```c\n";
    }
    return "";
}

static std::string fi_filter(const std::string& /*content*/, ParserState& state) {
    if (state.fill_depth > 0) {
        if (--state.fill_depth == 0) {
            return "```\n";
        }
    }
    return "";
}

static std::string in_filter(const std::string& content, ParserState& state) {
    if (content.empty()) {
        state.indent_stack.back() = 0;
    } else {
        std::string arg = remove_quotes(content);
        std::string num_str;
        for (char c : arg) {
            if (std::isdigit(c) || c == '+' || c == '-') {
                num_str += c;
            }
        }
        if (!num_str.empty()) {
            try {
                if (num_str.front() == '+') {
                    state.indent_stack.back() += std::stoi(num_str.substr(1));
                } else if (num_str.front() == '-') {
                    state.indent_stack.back() = std::max(0, state.indent_stack.back() - std::stoi(num_str.substr(1)));
                } else {
                    state.indent_stack.back() = std::max(0, std::stoi(num_str));
                }
            } catch (...) {
            }
        }
    }
    return "";
}

static std::string rs_filter(const std::string& content, ParserState& state) {
    int new_indent = state.current_indent() + 4;
    if (!content.empty()) {
        std::string arg = remove_quotes(content);
        std::string num_str;
        for (char c : arg) {
            if (std::isdigit(c) || c == '+' || c == '-') {
                num_str += c;
            }
        }
        if (!num_str.empty()) {
            try {
                if (num_str.front() == '+') {
                    new_indent = state.current_indent() + std::stoi(num_str.substr(1));
                } else if (num_str.front() == '-') {
                    new_indent = std::max(0, state.current_indent() - std::stoi(num_str.substr(1)));
                } else {
                    new_indent = std::max(0, std::stoi(num_str));
                }
            } catch (...) {}
        }
    }
    state.indent_stack.push_back(new_indent);
    return "\n";
}

static std::string re_filter(const std::string& /*content*/, ParserState& state) {
    state.indent_stack.pop_back();
    if (state.indent_stack.empty()) {
        state.indent_stack.push_back(0);
    }
    return "\n";
}

static std::string ts_filter(const std::string& /*content*/, ParserState& state) {
    state.in_table = true;
    return "";
}

static bool is_simple_table(const std::vector<std::string>& lines) {
    bool in_options = false;
    bool in_format = true;
    
    if (!lines.empty()) {
        size_t semi = lines[0].find(';');
        size_t dot = lines[0].find('.');
        if (semi != std::string::npos && (dot == std::string::npos || semi < dot)) {
            in_options = true;
        }
    }

    for (const auto& line : lines) {
        if (in_options) {
            if (line.find(';') != std::string::npos) {
                in_options = false;
            }
            continue;
        }

        if (in_format) {
            for (char c : line) {
                if (c == 's' || c == 'S' || c == '^') {
                    return false;
                }
            }
            if (line.find("T{") != std::string::npos) {
                return false;
            }
            if (!line.empty() && line.back() == '.') {
                in_format = false;
            }
        } else {
            if (line.find("T{") != std::string::npos) {
                return false;
            }
        }
    }
    return true;
}

static std::string process_table_markdown(const std::vector<std::string>& lines) {
    char delim = '\t';
    bool in_options = false;
    bool in_format = true;
    size_t data_start_idx = 0;

    if (!lines.empty()) {
        size_t semi = lines[0].find(';');
        size_t dot = lines[0].find('.');
        if (semi != std::string::npos && (dot == std::string::npos || semi < dot)) {
            in_options = true;
            size_t tab_pos = lines[0].find("tab(");
            if (tab_pos != std::string::npos && tab_pos + 4 < lines[0].length()) {
                delim = lines[0][tab_pos + 4];
            }
        }
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        if (in_options) {
            if (lines[i].find(';') != std::string::npos) {
                in_options = false;
            }
            continue;
        }

        if (in_format) {
            if (!lines[i].empty() && lines[i].back() == '.') {
                in_format = false;
                data_start_idx = i + 1;
                break;
            }
        }
    }

    std::string result = "\n";
    bool header_done = false;
    size_t num_cols = 0;

    for (size_t i = data_start_idx; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (line.empty() || line == "_" || line == "=" || line.starts_with(".\\\"")) {
            continue;
        }

        std::vector<std::string> cols;
        std::stringstream ss(line);
        std::string item;
        while (std::getline(ss, item, delim)) {
            cols.push_back(item);
        }
        if (!line.empty() && line.back() == delim) {
            cols.push_back("");
        }

        if (!header_done) {
            num_cols = cols.size();
            result += "|";
            for (const auto& c : cols) {
                result += " " + c + " |";
            }
            result += "\n|";
            for (size_t j = 0; j < num_cols; ++j) {
                result += "---|";
            }
            result += "\n";
            header_done = true;
        } else {
            result += "|";
            for (size_t j = 0; j < num_cols; ++j) {
                if (j < cols.size()) {
                    result += " " + cols[j] + " |";
                } else {
                    result += " |";
                }
            }
            result += "\n";
        }
    }
    return result + "\n";
}

static std::string process_table_tabs(const std::vector<std::string>& lines) {
    char delim = '\t';
    bool in_options = false;
    bool in_format = true;
    size_t data_start_idx = 0;

    if (!lines.empty()) {
        size_t semi = lines[0].find(';');
        size_t dot = lines[0].find('.');
        if (semi != std::string::npos && (dot == std::string::npos || semi < dot)) {
            in_options = true;
            size_t tab_pos = lines[0].find("tab(");
            if (tab_pos != std::string::npos && tab_pos + 4 < lines[0].length()) {
                delim = lines[0][tab_pos + 4];
            }
        }
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        if (in_options) {
            if (lines[i].find(';') != std::string::npos) {
                in_options = false;
            }
            continue;
        }

        if (in_format) {
            if (!lines[i].empty() && lines[i].back() == '.') {
                in_format = false;
                data_start_idx = i + 1;
                break;
            }
        }
    }

    std::string result = "\n```text\n";
    for (size_t i = data_start_idx; i < lines.size(); ++i) {
        std::string line = lines[i];
        if (line.starts_with(".\\\"")) {
            continue;
        }
        if (delim != '\t') {
            std::replace(line.begin(), line.end(), delim, '\t');
        }
        result += line + "\n";
    }
    result += "```\n";
    return result;
}

static std::string process_table(const std::vector<std::string>& lines) {
    if (is_simple_table(lines)) {
        return process_table_markdown(lines);
    } else {
        return process_table_tabs(lines);
    }
}

using FilterFunc = std::function<std::string(const std::string&, ParserState&)>;

static const std::unordered_map<std::string, FilterFunc> dispatch_table = {
    {".TH", comment_filter},
    {".PD", comment_filter},
    {".na", comment_filter},
    {".nh", comment_filter},
    {".ad", comment_filter},
    {".el", comment_filter},
    {".ie", comment_filter},
    {".SH", sh_filter},
    {".Sh", sh_filter},
    {".SS", ss_filter},
    {".B", b_filter},
    {".I", i_filter},
    {".BR", br_filter},
    {".BI", bi_filter},
    {".IR", ir_filter},
    {".RI", ri_filter},
    {".RB", rb_filter},
    {".IB", ib_filter},
    {".P", p_filter},
    {".PP", p_filter},
    {".Pp", p_filter},
    {".HP", p_filter},
    {".LP", p_filter},
    {".bP", p_filter},
    {".sp", p_filter},
    {".TP", tp_filter},
    {".IP", ip_filter},
    {".TQ", tq_filter},
    {".nf", nf_filter},
    {".fi", fi_filter},
    {".EX", nf_filter},
    {".EE", fi_filter},
    {".in", in_filter},
    {".RS", rs_filter},
    {".RE", re_filter},
    {".TS", ts_filter},
    {".ft", ft_filter},
    {".if", comment_filter},
    {".\\}", comment_filter},
    {".el\\{\\", comment_filter},
    {".", comment_filter},
    {".de", comment_filter},
    {"..", comment_filter},
    {".nr", comment_filter},
    {".rr", comment_filter},
    {".ds", comment_filter},
    {".br", p_filter},
    {".ne", comment_filter},
    {".IX", comment_filter},
    {".Vb", nf_filter},
    {".Ve", fi_filter},
    {".hw", comment_filter},
    {".lf", comment_filter},
    {".Sp", p_filter},
    {".ns", comment_filter},
    {".rs", comment_filter},
    {".hy", comment_filter},
    {".tr", comment_filter},
    {".de1", comment_filter},
    {".ZN", pass_through_filter},
    {".UNINDENT", re_filter},
    {".INDENT", rs_filter},
    {".TA", comment_filter},
    {".QS", rs_filter},
    {".QE", re_filter},
    {".QC", rs_filter},
    {".PS", comment_filter},
    {".PE", comment_filter},
    {".PC", comment_filter},
    {".ta", comment_filter},
    {".ti", comment_filter},
    {".it", comment_filter},
    {".SM", comment_filter},
    {".so", comment_filter},
    {".rm", comment_filter},
    {".ps", comment_filter},
    {".UC", comment_filter},
    {".ss", comment_filter},
    {".als", comment_filter},
    {".fam", comment_filter},
    {".ll", comment_filter},
    {".UR", comment_filter},
    {".UE", comment_filter},
    {".URL", comment_filter},
    {".MTO", comment_filter},
    {".YS", comment_filter},
    {".SY", comment_filter},
    {".Op", comment_filter},
    {".Nm", comment_filter},
    {".Nd", comment_filter},
    {".Dt", comment_filter},
    {".Dd", comment_filter},
    {".Xr", comment_filter},
    {"", pass_through_filter}
};

std::string troff2md(std::string troff_content) {
    ParserState state;
    std::vector<std::string> input_lines = split_lines(troff_content);
    std::string output;
    std::set<std::string> unhandled_commands;

    for (const auto& line : input_lines) {
        if (state.in_table) {
            if (line.starts_with(".TE")) {
                state.in_table = false;
                output += process_table(state.table_lines);
                state.table_lines.clear();
            } else {
                state.table_lines.push_back(line);
            }
            continue;
        }

        if (line.starts_with(".\\\"")) {
            continue;
        }

        std::string command = "";
        std::string content = line;

        if (!line.empty() && line[0] == '.') {
            size_t space_pos = line.find_first_of(" \t");
            if (space_pos != std::string::npos) {
                command = line.substr(0, space_pos);
                size_t content_start = line.find_first_not_of(" \t", space_pos);
                if (content_start != std::string::npos) {
                    content = line.substr(content_start);
                } else {
                    content = "";
                }
            } else {
                command = line;
                content = "";
            }
        }

        if (state.tp_state == TPState::IN_BODY && !command.empty()) {
            if (structural_macros.contains(command)) {
                state.tp_state = TPState::NORMAL;
            }
        }

        std::string line_output;
        auto it = dispatch_table.find(command);
        if (it != dispatch_table.end()) {
            line_output = it->second(content, state);
        } else {
            // Pass-through unhandled commands
            if (!command.empty()) {
                unhandled_commands.insert(command);
            }
            line_output = line + '\n';
        }

        line_output = process_escapes(line_output, state);

        std::string prefix = std::string(state.current_indent(), ' ');
        if (line_output.starts_with("```")) {
            prefix = "";
            if (!output.empty() && output.back() != '\n') {
                output += '\n';
            }
        }

        if (state.tp_state == TPState::EXPECTING_TAG && command != ".TP" && command != ".TQ") {
            if (!line_output.empty() && line_output.back() != '\n') {
                line_output += '\n';
            }
            if (line_output.starts_with("```")) {
                output += line_output;
            } else {
                prefix = std::string(state.current_indent(), ' ');
                output += prefix + "* " + line_output;
            }
            state.tp_state = TPState::IN_BODY;
        } else if (state.tp_state == TPState::IN_BODY && command != ".IP" && command != ".TQ") {
            if (line_output == "\n" || line_output.empty()) {
                output += line_output;
            } else {
                output += prefix + "  " + line_output;
            }
        } else {
            if (line_output.starts_with("```") || command == ".EX") {
                output += prefix + line_output;
            } else if (line_output != "\n" && !line_output.empty()) {
                output += prefix + line_output;
            } else {
                output += line_output;
            }
        }
    }

    if (state.fill_depth > 0) {
        if (!output.empty() && output.back() != '\n') {
            output += '\n';
        }
        output += "```\n";
    }

#ifdef MAN2MD_DEBUG_MACROS
    if (!unhandled_commands.empty()) {
        std::cerr << "--- Unhandled troff commands ---\n";
        for (const auto& cmd : unhandled_commands) {
            std::cerr << cmd << '\n';
        }
        std::cerr << "--------------------------------\n";
    }
#endif

    if (!output.empty() && output.back() != '\n') {
        output += '\n';
    }

    return output;
}
