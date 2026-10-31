#include "call_synthesizer.h"
#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <ctre.hpp>
#include <sstream>
#include <unordered_set>
#include "agentlib/ai_model.h"
#include "agentlib/copilot_manager.h"
#include "agentlib/httplib_transport.h"
#include "agentlib/llm_client.h"
#include "event_logger.h"
#include "languages/cpp/keywords.h"
#include "codemap_utils.h"
#include "lsp_manager.h"
#include "project_manager.h"

namespace turbostar
{

namespace
{

std::string trim_string(std::string_view sv)
{
	while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
		sv.remove_prefix(1);
	}
	while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
		sv.remove_suffix(1);
	}
	return std::string(sv);
}

std::string normalize_name(std::string_view sv)
{
	std::string s;
	s.reserve(sv.size());
	for (char c : sv) {
		if (c != '_') {
			s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
	}
	return s;
}

std::string normalize_type(std::string_view sv)
{
	std::string s = trim_string(sv);
	// Remove const, &, *
	std::string res;
	res.reserve(s.size());
	for (char c : s) {
		if (c != '&' && c != '*') {
			res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
	}
	res = trim_string(res);
	if (res.starts_with("const ")) {
		res = trim_string(res.substr(6));
	}
	return res;
}

} // namespace

std::vector<variable_info> call_synthesizer::extract_in_scope_variables(const document &doc, int target_line)
{
	std::vector<variable_info> vars;
	std::unordered_set<std::string> seen_names;

	int total_lines = static_cast<int>(doc.get_line_count());
	if (target_line < 0 || target_line >= total_lines) {
		return vars;
	}

	// 1. Walk backwards to find opening brace of the enclosing function
	int func_start_line = 0;
	int brace_depth = 0;
	for (int line_idx = target_line; line_idx >= 0; --line_idx) {
		auto l = doc.get_line(line_idx);
		if (!l)
			continue;
		std::string text = l->get_text();
		for (auto it = text.rbegin(); it != text.rend(); ++it) {
			if (*it == '}') {
				brace_depth++;
			} else if (*it == '{') {
				if (brace_depth == 0) {
					func_start_line = line_idx;
					break;
				}
				brace_depth--;
			}
		}
		if (func_start_line != 0) {
			break;
		}
	}

	// 2. Parse function header parameters if present
	if (func_start_line > 0) {
		std::string header_text;
		for (int line_idx = std::max(0, func_start_line - 3); line_idx <= func_start_line; ++line_idx) {
			auto l = doc.get_line(line_idx);
			if (l) {
				header_text += " " + l->get_text();
			}
		}
		size_t open_p = header_text.find('(');
		size_t close_p = header_text.find(')', open_p);
		if (open_p != std::string::npos && close_p != std::string::npos && close_p > open_p) {
			std::string params_str = header_text.substr(open_p + 1, close_p - open_p - 1);
			std::stringstream ss(params_str);
			std::string token;
			while (std::getline(ss, token, ',')) {
				token = trim_string(token);
				if (token.empty() || token == "void")
					continue;
				size_t last_space = token.find_last_of(" \t*&");
				if (last_space != std::string::npos && last_space + 1 < token.size()) {
					std::string ptype = trim_string(token.substr(0, last_space + 1));
					std::string pname = trim_string(token.substr(last_space + 1));
					if (!pname.empty() && seen_names.insert(pname).second) {
						vars.push_back({pname, ptype, func_start_line});
					}
				}
			}
		}
	}

	// 3. Scan local lines from func_start_line up to target_line
	for (int line_idx = func_start_line; line_idx <= target_line; ++line_idx) {
		auto l = doc.get_line(line_idx);
		if (!l)
			continue;
		std::string text = l->get_text();

		if (auto m = ctre::search<R"(^\s*(?:const\s+)?([a-zA-Z_][a-zA-Z0-9_:<>*&]*)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:=|;|\{))">(text)) {
			std::string vtype(m.get<1>().to_view());
			std::string vname(m.get<2>().to_view());
			if (vname != "return" && vname != "if" && vname != "while" && vname != "for" && vname != "switch") {
				if (seen_names.insert(vname).second) {
					vars.push_back({vname, vtype, line_idx});
				}
			}
		}
		if (auto m = ctre::search<R"(for\s*\(\s*(?:const\s+)?(?:auto|int|size_t|[a-zA-Z_][a-zA-Z0-9_]*)[*&\s]+([a-zA-Z_][a-zA-Z0-9_]*))">(text)) {
			std::string vname(m.get<1>().to_view());
			if (seen_names.insert(vname).second) {
				vars.push_back({vname, "auto", line_idx});
			}
		}
	}

	// 4. Scan for common member variables ending in '_' across nearby lines
	for (int line_idx = std::max(0, target_line - 50); line_idx <= std::min(total_lines - 1, target_line + 50); ++line_idx) {
		auto l = doc.get_line(line_idx);
		if (!l)
			continue;
		std::string text = l->get_text();
		for (auto m : ctre::search_all<R"(\b([a-zA-Z_][a-zA-Z0-9_]*_)\b)">(text)) {
			std::string mname(m.get<1>().to_view());
			if (seen_names.insert(mname).second) {
				vars.push_back({mname, "auto", line_idx});
			}
		}
	}

	return vars;
}

std::optional<signature_info> call_synthesizer::parse_function_signature(std::string_view raw_signature)
{
	std::string raw = trim_string(raw_signature);
	if (raw.empty()) {
		return std::nullopt;
	}

	size_t open_paren = raw.find('(');
	if (open_paren == std::string::npos) {
		return std::nullopt;
	}

	size_t close_paren = raw.rfind(')');
	if (close_paren == std::string::npos || close_paren <= open_paren) {
		return std::nullopt;
	}

	std::string pre = trim_string(raw.substr(0, open_paren));
	size_t last_space = pre.find_last_of(" \t*&");
	std::string func_name = (last_space != std::string::npos) ? trim_string(pre.substr(last_space + 1)) : pre;
	std::string return_type = (last_space != std::string::npos) ? trim_string(pre.substr(0, last_space + 1)) : "";

	// Remove trailing scope qualifiers from func_name if desired, or keep as is
	signature_info sig;
	sig.function_name = func_name;
	sig.return_type = return_type;
	sig.raw_signature = raw;

	std::string_view param_section = std::string_view(raw).substr(open_paren + 1, close_paren - open_paren - 1);
	
	// Split parameters by ',' taking template depth into account
	std::vector<std::string> param_strs;
	int template_depth = 0;
	size_t start = 0;
	for (size_t i = 0; i < param_section.size(); ++i) {
		char c = param_section[i];
		if (c == '<') {
			template_depth++;
		} else if (c == '>') {
			if (template_depth > 0)
				template_depth--;
		} else if (c == ',' && template_depth == 0) {
			param_strs.push_back(trim_string(param_section.substr(start, i - start)));
			start = i + 1;
		}
	}
	if (start < param_section.size()) {
		std::string last_p = trim_string(param_section.substr(start));
		if (!last_p.empty() && last_p != "void") {
			param_strs.push_back(last_p);
		}
	}

	for (const auto &pstr : param_strs) {
		std::string decl = pstr;
		std::string def_val;
		size_t eq_pos = decl.find('=');
		if (eq_pos != std::string::npos) {
			def_val = trim_string(decl.substr(eq_pos + 1));
			decl = trim_string(decl.substr(0, eq_pos));
		}

		size_t name_split = decl.find_last_of(" \t*&");
		std::string ptype, pname;
		if (name_split != std::string::npos && name_split + 1 < decl.size()) {
			ptype = trim_string(decl.substr(0, name_split + 1));
			pname = trim_string(decl.substr(name_split + 1));
		} else {
			ptype = decl;
			pname = "";
		}

		sig.parameters.push_back({ptype, pname, def_val});
	}

	return sig;
}

int call_synthesizer::score_variable_match(std::string_view param_name, std::string_view param_type, const variable_info &var)
{
	int score = 0;
	std::string p_norm = normalize_name(param_name);
	std::string v_norm = normalize_name(var.name);

	if (!p_norm.empty() && !v_norm.empty()) {
		if (param_name == var.name) {
			score += 100;
		} else if (p_norm == v_norm) {
			score += 90;
		} else if (p_norm.find(v_norm) != std::string::npos || v_norm.find(p_norm) != std::string::npos) {
			score += 60;
		}
	}

	std::string p_type_norm = normalize_type(param_type);
	std::string v_type_norm = normalize_type(var.type);

	if (!p_type_norm.empty() && !v_type_norm.empty() && v_type_norm != "auto") {
		if (p_type_norm == v_type_norm) {
			score += 45;
		} else if ((p_type_norm.find("string") != std::string::npos && v_type_norm.find("string") != std::string::npos) ||
		           (p_type_norm == "int" && v_type_norm == "size_t") ||
		           (p_type_norm == "size_t" && v_type_norm == "int")) {
			score += 40;
		}
	}

	return score;
}

std::vector<call_suggestion> call_synthesizer::generate_argument_suggestions(
    const signature_info &sig,
    const std::vector<variable_info> &locals,
    std::string_view existing_args)
{
	std::vector<call_suggestion> suggestions;

	if (sig.parameters.empty()) {
		suggestions.push_back({"()", "()", 100, false});
		return suggestions;
	}

	// Special handling for formatting functions: printf, fprintf, sprintf, snprintf
	if (sig.function_name == "printf" || sig.function_name == "fprintf" ||
	    sig.function_name == "sprintf" || sig.function_name == "snprintf") {
		std::vector<std::string> str_vars;
		std::vector<std::string> int_vars;
		std::vector<std::string> ptr_vars;

		for (const auto &v : locals) {
			std::string t = normalize_type(v.type);
			if (t.find("string") != std::string::npos || t.find("char*") != std::string::npos || t.find("char *") != std::string::npos) {
				str_vars.push_back(v.name);
			} else if (t == "int" || t == "size_t" || t == "int64_t" || t == "uint32_t" || t == "long" || t == "short") {
				int_vars.push_back(v.name);
			} else if (t.find('*') != std::string::npos) {
				ptr_vars.push_back(v.name);
			}
		}

		std::string prefix;
		if (sig.function_name == "fprintf") {
			prefix = "stderr, ";
		} else if (sig.function_name == "sprintf") {
			prefix = (!ptr_vars.empty() ? ptr_vars[0] : (!str_vars.empty() ? str_vars[0] : "buf")) + ", ";
		} else if (sig.function_name == "snprintf") {
			std::string b = (!ptr_vars.empty() ? ptr_vars[0] : (!str_vars.empty() ? str_vars[0] : "buf"));
			prefix = b + ", sizeof(" + b + "), ";
		}

		std::string trimmed_existing = trim_string(existing_args);
		if (!trimmed_existing.empty()) {
			if (trimmed_existing.ends_with(',')) {
				if (!int_vars.empty()) {
					std::string s = "(" + trimmed_existing + " " + int_vars[0] + ")";
					suggestions.push_back({s, s, 95, false});
				}
				if (!str_vars.empty()) {
					std::string s = "(" + trimmed_existing + " " + str_vars[0] + ")";
					suggestions.push_back({s, s, 90, false});
				}
			} else {
				suggestions.push_back({"(" + trimmed_existing + ")", "(" + trimmed_existing + ")", 100, false});
				if (!trimmed_existing.contains("\\n") && trimmed_existing.ends_with('"')) {
					std::string with_nl = trimmed_existing.substr(0, trimmed_existing.size() - 1) + "\\n\"";
					suggestions.push_back({"(" + with_nl + ")", "(" + with_nl + ")", 95, false});
				}
				if (trimmed_existing.ends_with('"')) {
					if (!int_vars.empty()) {
						std::string s = "(" + trimmed_existing + ", " + int_vars[0] + ")";
						suggestions.push_back({s, s, 90, false});
					}
					if (!str_vars.empty()) {
						std::string s = "(" + trimmed_existing + ", " + str_vars[0] + ")";
						suggestions.push_back({s, s, 85, false});
					}
				}
			}
		} else {
			if (!str_vars.empty() && !int_vars.empty()) {
				std::string s1 = std::format("({}\"%s: %d\\n\", {}, {})", prefix, str_vars[0], int_vars[0]);
				std::string s2 = std::format("({}\"%s\\n\", {})", prefix, str_vars[0]);
				suggestions.push_back({s1, s1, 95, false});
				suggestions.push_back({s2, s2, 90, false});
			} else if (!str_vars.empty()) {
				std::string s1 = std::format("({}\"%s\\n\", {})", prefix, str_vars[0]);
				std::string s2 = std::format("({}\"Hello, world!\\n\")", prefix);
				suggestions.push_back({s1, s1, 95, false});
				suggestions.push_back({s2, s2, 85, false});
			} else if (!int_vars.empty()) {
				std::string s1 = std::format("({}\"%d\\n\", {})", prefix, int_vars[0]);
				std::string s2 = std::format("({}\"count = %d\\n\", {})", prefix, int_vars[0]);
				suggestions.push_back({s1, s1, 95, false});
				suggestions.push_back({s2, s2, 90, false});
			} else {
				std::string s1 = std::format("({}\"\\n\")", prefix);
				std::string s2 = std::format("({}\"Hello, world!\\n\")", prefix);
				suggestions.push_back({s1, s1, 80, false});
				suggestions.push_back({s2, s2, 75, false});
			}
		}

		if (!suggestions.empty()) {
			return suggestions;
		}
	}

	// Parse any already-provided arguments to bias suggestions
	std::vector<std::string> existing_tokens;
	if (!existing_args.empty()) {
		std::stringstream ss{std::string(existing_args)};
		std::string tok;
		while (std::getline(ss, tok, ',')) {
			tok = trim_string(tok);
			if (!tok.empty()) {
				existing_tokens.push_back(tok);
			}
		}
	}

	// Heuristic combination 1: Best scoring local variables
	std::vector<std::string> args1;
	std::vector<std::string> args2;
	int total_score1 = 0;
	int total_score2 = 0;

	for (size_t pidx = 0; pidx < sig.parameters.size(); ++pidx) {
		const auto &param = sig.parameters[pidx];

		// If user already typed an argument for this position, preserve it!
		if (pidx < existing_tokens.size()) {
			args1.push_back(existing_tokens[pidx]);
			args2.push_back(existing_tokens[pidx]);
			total_score1 += 100;
			total_score2 += 100;
			continue;
		}

		std::vector<std::pair<int, std::string>> ranked;
		for (const auto &var : locals) {
			int sc = score_variable_match(param.name, param.type, var);
			if (sc > 30) {
				ranked.emplace_back(sc, var.name);
			}
		}

		std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

		// Choice for args1
		if (!ranked.empty()) {
			args1.push_back(ranked[0].second);
			total_score1 += ranked[0].first;
		} else if (!param.default_value.empty()) {
			args1.push_back(param.default_value);
			total_score1 += 50;
		} else {
			// Fallback literal based on parameter type/name
			std::string p_norm = normalize_name(param.name);
			std::string t_norm = normalize_type(param.type);
			if (t_norm == "int" || t_norm == "size_t") {
				if (p_norm.find("width") != std::string::npos || p_norm == "w") {
					args1.push_back("width_");
				} else if (p_norm.find("height") != std::string::npos || p_norm == "h") {
					args1.push_back("height_");
				} else if (p_norm == "x") {
					args1.push_back("x_");
				} else if (p_norm == "y") {
					args1.push_back("y_");
				} else if (p_norm.find("pair") != std::string::npos) {
					args1.push_back("1");
				} else {
					args1.push_back("0");
				}
			} else if (t_norm == "bool") {
				args1.push_back("false");
			} else if (t_norm.find("string") != std::string::npos) {
				args1.push_back("\"\"");
			} else {
				args1.push_back(!param.name.empty() ? param.name : "/* " + param.type + " */");
			}
			total_score1 += 20;
		}

		// Choice for args2 (alternative ranked candidate or default)
		if (ranked.size() > 1) {
			args2.push_back(ranked[1].second);
			total_score2 += ranked[1].first;
		} else if (!param.default_value.empty() && (ranked.empty() || args1.back() != param.default_value)) {
			args2.push_back(param.default_value);
			total_score2 += 50;
		} else {
			// If args1 used a variable, try literal in args2
			std::string p_norm = normalize_name(param.name);
			if (p_norm == "x" || p_norm == "y") {
				args2.push_back("0");
			} else if (p_norm == "width" || p_norm == "w") {
				args2.push_back("COLS");
			} else if (p_norm == "height" || p_norm == "h") {
				args2.push_back("LINES");
			} else {
				args2.push_back(args1.back());
			}
			total_score2 += 20;
		}
	}

	auto format_call = [](const std::vector<std::string> &args) -> std::string {
		std::string res = "(";
		for (size_t i = 0; i < args.size(); ++i) {
			if (i > 0)
				res += ", ";
			res += args[i];
		}
		res += ")";
		return res;
	};

	std::string call1 = format_call(args1);
	suggestions.push_back({call1, call1, total_score1, false});

	std::string call2 = format_call(args2);
	if (call2 != call1) {
		suggestions.push_back({call2, call2, total_score2, false});
	}

	return suggestions;
}

context_menu_data call_synthesizer::analyze_context_at(document &doc, int doc_y, int doc_x, bool allow_blocking_lsp)
{
	context_menu_data data;
	data.target_line = doc_y;

	if (doc_y < 0 || doc_y >= static_cast<int>(doc.get_line_count())) {
		return data;
	}

	auto l = doc.get_line(doc_y);
	if (!l)
		return data;

	std::string line_text = l->get_text();
	if (line_text.empty() || doc_x < 0) {
		return data;
	}

	// 1. Identify token under cursor or enclosing call site
	int start_x = std::min(doc_x, static_cast<int>(line_text.size()));
	int end_x = start_x;

	bool on_ident = (start_x < static_cast<int>(line_text.size()) &&
			 (std::isalnum(static_cast<unsigned char>(line_text[start_x])) || line_text[start_x] == '_'));

	if (!on_ident && start_x > 0 &&
	    (std::isalnum(static_cast<unsigned char>(line_text[start_x - 1])) || line_text[start_x - 1] == '_')) {
		// Cursor right after an identifier (e.g. "printf|")
		start_x--;
		on_ident = true;
	}

	if (on_ident) {
		while (start_x > 0 &&
		       (std::isalnum(static_cast<unsigned char>(line_text[start_x - 1])) || line_text[start_x - 1] == '_')) {
			start_x--;
		}
		end_x = start_x;
		while (end_x < static_cast<int>(line_text.size()) &&
		       (std::isalnum(static_cast<unsigned char>(line_text[end_x])) || line_text[end_x] == '_')) {
			end_x++;
		}
		data.token = line_text.substr(start_x, end_x - start_x);
		data.token_start_col = start_x;
		data.token_end_col = end_x;
	} else {
		// Cursor is not on an identifier. It might be on '(' or inside parens: e.g. "printf(|)"
		int scan_p = std::min(doc_x, static_cast<int>(line_text.size()) - 1);
		int open_p = -1;
		int depth = 0;
		for (int i = scan_p; i >= 0; --i) {
			if (line_text[i] == ')') {
				depth++;
			} else if (line_text[i] == '(') {
				if (depth > 0) {
					depth--;
				} else {
					open_p = i;
					break;
				}
			}
		}
		if (open_p != -1) {
			int fn_end = open_p;
			while (fn_end > 0 && std::isspace(static_cast<unsigned char>(line_text[fn_end - 1]))) {
				fn_end--;
			}
			int fn_start = fn_end;
			while (fn_start > 0 &&
			       (std::isalnum(static_cast<unsigned char>(line_text[fn_start - 1])) || line_text[fn_start - 1] == '_')) {
				fn_start--;
			}
			if (fn_end > fn_start) {
				data.token = line_text.substr(fn_start, fn_end - fn_start);
				data.token_start_col = fn_start;
				data.token_end_col = fn_end;
			}
		}
	}

	if (data.token.empty()) {
		return data;
	}

	// 2. Check for parentheses following data.token
	size_t check_p = data.token_end_col;
	while (check_p < line_text.size() && std::isspace(static_cast<unsigned char>(line_text[check_p]))) {
		check_p++;
	}

	int open_paren = -1;
	int close_paren = -1;
	if (check_p < line_text.size() && line_text[check_p] == '(') {
		open_paren = static_cast<int>(check_p);
		data.has_existing_parens = true;
		data.existing_args_start_col = open_paren;

		int depth = 1;
		for (size_t i = open_paren + 1; i < line_text.size(); ++i) {
			if (line_text[i] == '(') {
				depth++;
			} else if (line_text[i] == ')') {
				depth--;
				if (depth == 0) {
					close_paren = static_cast<int>(i);
					break;
				}
			} else if (line_text[i] == ';' && depth > 0) {
				break;
			}
		}
		data.existing_args_end_col = close_paren;

		if (close_paren != -1) {
			data.call_case = call_site_case::full_call;
			if (close_paren > open_paren + 1) {
				data.existing_args = line_text.substr(open_paren + 1, close_paren - open_paren - 1);
			}
		} else {
			data.call_case = call_site_case::half_call;
			size_t end_arg = line_text.size();
			size_t semi = line_text.find(';', open_paren);
			if (semi != std::string::npos) {
				end_arg = semi;
			}
			if (end_arg > static_cast<size_t>(open_paren + 1)) {
				data.existing_args = line_text.substr(open_paren + 1, end_arg - (open_paren + 1));
			}
		}
	} else {
		data.call_case = call_site_case::naked_identifier;
	}

	// 3. Resolve signature: check non-blocking cache/builtins first
	std::string sig_str;
	auto cached_sig = tools::get_cached_function_signature(doc.get_filename(), doc_y, data.token_start_col, data.token);
	if (cached_sig) {
		sig_str = cached_sig->raw_signature;
		data.declaration_location = cached_sig->declaration_location;
		data.type_synopsis = cached_sig->raw_signature;
		data.is_resolved = true;
	} else if (allow_blocking_lsp) {
		auto sig_res = tools::resolve_function_signature(doc.get_filename(), doc_y, data.token_start_col, data.token);
		if (sig_res) {
			sig_str = sig_res->raw_signature;
			data.declaration_location = sig_res->declaration_location;
			data.type_synopsis = sig_res->raw_signature;
			data.is_resolved = true;
		}
	}

	if (!sig_str.empty()) {
		auto parsed_sig = parse_function_signature(sig_str);
		if (parsed_sig) {
			data.kind = symbol_kind_context::function_call;
			auto locals = extract_in_scope_variables(doc, doc_y);
			data.suggestions = generate_argument_suggestions(*parsed_sig, locals, data.existing_args);
			return data;
		}
	}

	if (data.is_resolved) {
		return data;
	}

	// If not yet resolved, assign preliminary kind without blocking on LSP
	if (data.has_existing_parens) {
		data.kind = symbol_kind_context::function_call;
		data.type_synopsis = data.token + "(...)";
		auto locals = extract_in_scope_variables(doc, doc_y);
		signature_info dummy_sig;
		dummy_sig.function_name = data.token;
		data.suggestions = generate_argument_suggestions(dummy_sig, locals, data.existing_args);
	} else if (allow_blocking_lsp) {
		auto defs = project_manager::get_instance().lsp_query_definition(doc.get_filename(), doc_y, data.token_start_col);
		if (!defs.empty() && (defs[0].kind == "class" || defs[0].kind == "struct" || defs[0].kind == "enum" ||
				      defs[0].kind == "interface" || defs[0].kind == "type")) {
			data.kind = symbol_kind_context::type_definition;
			if (!defs[0].underlying_type.empty()) {
				data.type_synopsis = defs[0].underlying_type;
			}
			if (!defs[0].path.empty()) {
				data.declaration_location = std::format("{}:{}", defs[0].path, defs[0].range.start_y + 1);
			}
			data.is_resolved = true;
		} else {
			data.kind = symbol_kind_context::variable;
			if (!defs.empty() && !defs[0].path.empty()) {
				data.declaration_location = std::format("{}:{}", defs[0].path, defs[0].range.start_y + 1);
			}
			data.is_resolved = true;
		}
	} else {
		data.kind = symbol_kind_context::unknown;
	}

	return data;
}

bool call_synthesizer::has_ai_model_configured()
{
	auto model = agentlib::ai_model_registry::get_instance().get_default_model();
	return (model != nullptr && !model->get_url().empty());
}

std::optional<std::string> call_synthesizer::query_ai_call_suggestion(
    std::string_view enclosing_function_code,
    std::string_view raw_signature,
    std::span<const variable_info> in_scope_vars,
    std::string_view existing_args)
{
	auto default_model = agentlib::ai_model_registry::get_instance().get_default_model();
	if (!default_model || default_model->get_url().empty()) {
		return std::nullopt;
	}

	std::string vars_summary;
	for (const auto &v : in_scope_vars) {
		if (!vars_summary.empty()) {
			vars_summary += ", ";
		}
		vars_summary += v.type + " " + v.name;
	}

	std::vector<agentlib::message> convo;
	agentlib::message sys_msg;
	sys_msg.role = "system";
	sys_msg.content = "You are an intelligent C++ code completion engine. Given a function signature, variables in scope, and context, synthesize the single most idiomatic argument call list enclosed in parentheses. Output ONLY the call list, e.g. '(var1, var2)'. Never output markdown fences, backticks, or explanation.";
	convo.push_back(sys_msg);

	agentlib::message user_msg;
	user_msg.role = "user";
	user_msg.content = std::format(
	    "Target Function Signature: {}\n"
	    "Variables in scope: [{}]\n"
	    "Existing Arguments Prefix: {}\n"
	    "Context:\n```cpp\n{}\n```\n"
	    "Arguments:",
	    raw_signature, vars_summary, existing_args.empty() ? "(none)" : existing_args, enclosing_function_code);
	convo.push_back(user_msg);

	try {
		auto transport = std::make_shared<agentlib::httplib_transport>(default_model->get_url(), default_model->get_api_key());
		if (default_model->get_api_type() == agentlib::api_type::copilot) {
			transport->set_token_provider([]() {
				return agentlib::copilot_manager::get_instance().get_copilot_token();
			});
		}
		agentlib::llm_client client(transport, default_model->get_id(), default_model->get_api_type());
		agentlib::llm_chat_response res = client.send_chat(convo);
		std::string result = res.msg.content;

		size_t p1 = result.find('(');
		size_t p2 = result.rfind(')');
		if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
			return result.substr(p1, p2 - p1 + 1);
		}
		result = trim_string(result);
		if (!result.empty()) {
			return "(" + result + ")";
		}
	} catch (...) {
		return std::nullopt;
	}
	return std::nullopt;
}

} // namespace turbostar
