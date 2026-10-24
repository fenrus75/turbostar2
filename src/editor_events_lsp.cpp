#include <algorithm>
#include <chrono>
#include <fstream>
#include <lsp/json/json.h>
#include <ncurses.h>
#include <sstream>
#include "build_error_manager.h"
#include "config_manager.h"
#include "editor.h"
#include "event_logger.h"
#include "fs_utils.h"
#include "gcc_log_parser.h"
#include "history_manager.h"
#include "lsp_manager.h"
#include "call_synthesizer.h"
#include "project_manager.h"
#include "ui/popup_menu.h"

namespace fs = std::filesystem;

void editor::dispatch_event_lsp(const editor_event &ev)
{

	if (ev.type == event_type::lsp_hover_result) {
		std::string text = ev.payload;
		std::replace(text.begin(), text.end(), '\n', ' ');
		if (text.length() > static_cast<size_t>(COLS - 20)) {
			text = text.substr(0, COLS - 20) + "...";
		}
		set_status_message(text, status_priorities::HOVER);
		return;
	}

	if (ev.type == event_type::lsp_highlight_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc) {
			if (!ev.payload.empty()) {
				std::string safe_name = active_doc->get_safe_filename();
				std::string name = active_doc->get_filename();
				if (safe_name != ev.payload && name != ev.payload) {
					return;
				}
			}
			active_doc->set_lsp_highlights(ev.highlight_ranges);
		}
		return;
	}

	if (ev.type == event_type::lsp_selection_range_result) {
		std::shared_ptr<document> active_doc = get_active_doc();
		if (active_doc && !ev.highlight_ranges.empty()) {
			int total_lines = static_cast<int>(active_doc->line_count());

			// Always find and update the "optimal" enclosing scope (largest not whole file)
			const text_range *enclosing = nullptr;
			for (const auto &range : ev.highlight_ranges) {
				int range_height = range.end_y - range.start_y;
				// If range is almost the whole file (say > 95%), we stop at the previous one
				if (range_height >= total_lines - 2 && enclosing != nullptr) {
					break;
				}
				enclosing = &range;
			}
			if (enclosing) {
				active_doc->set_enclosing_scope(*enclosing);
			}

			if (pending_inline_agent_task_.active) {
				// Special logic for headless agent: Use the enclosing scope we just found
				if (enclosing) {
					active_doc->set_selection(enclosing->start_y, enclosing->start_x, enclosing->end_y,
								  enclosing->end_x);
				}

				launch_inline_agent(pending_inline_agent_task_.prompt);
				pending_inline_agent_task_.active = false;
				active_doc->clear_selection(); // Clear it after launch so user doesn't see it lingering
			} else {
				// Standard expansion logic: Innermost strictly larger than current
				int sel_start_x = -1, sel_start_y = -1, sel_end_x = -1, sel_end_y = -1;
				bool has_sel = active_doc->has_selection();
				if (has_sel) {
					active_doc->get_selection_range(sel_start_x, sel_start_y, sel_end_x, sel_end_y);
				}

				for (const auto &range : ev.highlight_ranges) {
					if (!has_sel) {
						active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
						break;
					} else {
						if (range.start_y < sel_start_y ||
						    (range.start_y == sel_start_y && range.start_x < sel_start_x) ||
						    range.end_y > sel_end_y || (range.end_y == sel_end_y && range.end_x > sel_end_x)) {

							active_doc->set_selection(range.start_y, range.start_x, range.end_y, range.end_x);
							break;
						}
					}
				}
			}

			global_queue_.push(event_type::redraw);
		}
		return;
	}

	if (ev.type == event_type::lsp_diagnostics_result) {
		const std::string &target_file = ev.payload;
		if (target_file.empty()) {
			return;
		}

		std::filesystem::path target_p(target_file);
		bool matched_any = false;
		for (auto &doc : documents_) {
			if (!doc) {
				continue;
			}
			std::string doc_path = doc->get_filename();
			std::string safe_doc_path = doc->get_safe_filename();
			if (doc_path.empty() && safe_doc_path.empty()) {
				continue;
			}

			bool matches = false;
			if (!safe_doc_path.empty() && safe_doc_path == target_file) {
				matches = true;
			} else if (!doc_path.empty() && doc_path == target_file) {
				matches = true;
			} else {
				try {
					std::filesystem::path p(!safe_doc_path.empty() ? safe_doc_path : doc_path);
					if (std::filesystem::exists(p) && std::filesystem::exists(target_p)) {
						if (std::filesystem::equivalent(p, target_p)) {
							matches = true;
						}
					}
					if (!matches && std::filesystem::weakly_canonical(p).string() ==
							    std::filesystem::weakly_canonical(target_p).string()) {
						matches = true;
					}
				} catch (...) {
					// Ignore invalid paths
				}
			}

			if (matches) {
				doc->set_lsp_diagnostics(ev.diagnostics);
				matched_any = true;
			}
		}

		if (matched_any) {
			global_queue_.push(event_type::redraw);
		}
		return;
	}

	if (ev.type == event_type::lsp_signature_result) {
		auto payload = std::static_pointer_cast<turbostar::lsp_signature_payload>(ev.generic_promise);
		if (!payload || payload->generation != rmb_menu_generation_ || !active_popup_) {
			return;
		}

		window *target_w = nullptr;
		for (const auto &win : windows_) {
			if (win->get_id() == payload->window_id) {
				target_w = win.get();
				break;
			}
		}
		if (!target_w)
			return;
		auto doc = target_w->get_document();
		if (!doc)
			return;

		auto ctx = payload->ctx;
		ctx.is_resolved = payload->resolved;
		ctx.declaration_location = payload->declaration_location;
		ctx.type_synopsis = payload->type_synopsis;

		std::vector<popup_menu_item> new_items;

		auto apply_call_mutation = [ctx](document &d, std::string_view call_text) {
			if (ctx.call_case == turbostar::call_site_case::full_call ||
			    (ctx.has_existing_parens && ctx.existing_args_end_col != -1)) {
				d.set_selection(ctx.target_line, ctx.existing_args_start_col,
						ctx.target_line, ctx.existing_args_end_col + 1);
				d.delete_selection();
				d.insert_text(std::string(call_text));
			} else if (ctx.call_case == turbostar::call_site_case::half_call ||
				   (ctx.has_existing_parens && ctx.existing_args_end_col == -1)) {
				auto l_obj = d.get_line(ctx.target_line);
				int len = l_obj ? l_obj->length_in_chars() : ctx.existing_args_start_col + 1;
				if (l_obj) {
					std::string lt = l_obj->get_text();
					size_t semi = lt.find(';', ctx.existing_args_start_col);
					if (semi != std::string::npos) {
						len = static_cast<int>(semi);
					}
				}
				d.set_selection(ctx.target_line, ctx.existing_args_start_col, ctx.target_line, len);
				d.delete_selection();
				d.insert_text(std::string(call_text));
			} else {
				// Naked identifier (Case 1)
				d.move_cursor(ctx.token_end_col - d.get_cursor_x(), ctx.target_line - d.get_cursor_y());
				d.insert_text(std::string(call_text));
			}
		};

		if (payload->is_function) {
			ctx.kind = turbostar::symbol_kind_context::function_call;
			auto parsed_sig = turbostar::call_synthesizer::parse_function_signature(payload->raw_signature);
			if (parsed_sig) {
				auto locals = turbostar::call_synthesizer::extract_in_scope_variables(*doc, payload->doc_y);
				ctx.suggestions =
				    turbostar::call_synthesizer::generate_argument_suggestions(*parsed_sig, locals, ctx.existing_args);
			}

			if (!ctx.type_synopsis.empty()) {
				std::string syn = ctx.type_synopsis;
				if (syn.length() > 44) {
					syn = syn.substr(0, 41) + "...";
				}
				new_items.push_back({0, syn, 0, false});
				new_items.push_back({0, "", 0, true});
			}

			// Native suggestions (Slots 1 and 2)
			for (size_t s_idx = 0; s_idx < std::min<size_t>(ctx.suggestions.size(), 2); ++s_idx) {
				const auto &s = ctx.suggestions[s_idx];
				char hotkey = static_cast<char>('1' + s_idx);
				std::string label = std::format("{}. {}", hotkey, s.call_text);
				popup_menu_item item{0, label, hotkey, false};
				item.on_select = [this, w_ptr = target_w, apply_call_mutation, call_text = s.call_text]() {
					auto d = w_ptr->get_document();
					if (!d)
						return;
					apply_call_mutation(*d, call_text);
					global_queue_.push(event_type::redraw);
				};
				new_items.push_back(item);
			}

			// Slot 3: AI Suggestion
			if (turbostar::call_synthesizer::has_ai_model_configured()) {
				int ai_item_index = static_cast<int>(new_items.size());
				popup_menu_item ai_item{0, "3. [ \u231b AI analyzing... ]", '3', false};
				ai_item.on_select = []() {};
				new_items.push_back(ai_item);

				std::string enclosing_func;
				int start_l = std::max(0, payload->doc_y - 25);
				int end_l = std::min(static_cast<int>(doc->get_line_count()), payload->doc_y + 20);
				for (int l = start_l; l < end_l; ++l) {
					auto l_obj = doc->get_line(l);
					if (l_obj) {
						enclosing_func += l_obj->get_text() + "\n";
					}
				}
				auto locals = turbostar::call_synthesizer::extract_in_scope_variables(*doc, payload->doc_y);
				std::string sig_str = !ctx.type_synopsis.empty() ? ctx.type_synopsis : ctx.token;
				std::string existing_args = ctx.existing_args;

				std::thread([this, gen = payload->generation, ai_item_index, w_ptr = target_w, apply_call_mutation,
					     enclosing_func = std::move(enclosing_func), sig_str = std::move(sig_str),
					     locals = std::move(locals), existing_args = std::move(existing_args)]() {
					auto ai_res = turbostar::call_synthesizer::query_ai_call_suggestion(
					    enclosing_func, sig_str, locals, existing_args);
					if (!ai_res.has_value() || ai_res->empty()) {
						return;
					}
					post_to_main_thread([this, gen, ai_item_index, w_ptr, apply_call_mutation,
							     call_text = *ai_res]() {
						if (rmb_menu_generation_ != gen || !active_popup_) {
							return;
						}
						std::string label = "3. [AI] " + call_text;
						active_popup_->update_item(ai_item_index, label, '3',
									   [this, w_ptr, apply_call_mutation, call_text]() {
										   auto d = w_ptr->get_document();
										   if (!d)
											   return;
										   apply_call_mutation(*d, call_text);
										   global_queue_.push(event_type::redraw);
									   });
						global_queue_.push(event_type::redraw);
					});
				}).detach();
			}

			if (!new_items.empty()) {
				new_items.push_back({0, "", 0, true});
			}

			if (!ctx.declaration_location.empty()) {
				popup_menu_item def_item{0, "Go to Definition", 'D', false};
				def_item.on_select = [this, loc = ctx.declaration_location]() {
					editor_event oev;
					oev.type = event_type::open_file;
					oev.payload = loc;
					global_queue_.push(oev);
				};
				new_items.push_back(def_item);
			}

			popup_menu_item ref_item{0, "Find References", 'R', false};
			ref_item.on_select = [this, fn = doc->get_filename(), doc_y = payload->doc_y,
					      doc_x = payload->doc_x]() {
				auto refs = project_manager::get_instance().lsp_query_references(fn, doc_y, doc_x);
				if (!refs.empty()) {
					editor_event oev;
					oev.type = event_type::open_file;
					oev.payload = std::format("{}:{}", refs[0].path, refs[0].range.start_y + 1);
					global_queue_.push(oev);
				}
			};
			new_items.push_back(ref_item);
		} else {
			// Variable or type definition
			if (!ctx.type_synopsis.empty()) {
				std::string syn = ctx.type_synopsis;
				if (syn.length() > 40) {
					syn = syn.substr(0, 37) + "...";
				}
				new_items.push_back({0, syn, 0, false});
				new_items.push_back({0, "", 0, true});
			}
			if (!ctx.declaration_location.empty()) {
				popup_menu_item def_item{0, "Go to Definition", 'D', false};
				def_item.on_select = [this, loc = ctx.declaration_location]() {
					editor_event oev;
					oev.type = event_type::open_file;
					oev.payload = loc;
					global_queue_.push(oev);
				};
				new_items.push_back(def_item);
			}
			popup_menu_item ref_item{0, "Find References", 'R', false};
			ref_item.on_select = [this, fn = doc->get_filename(), doc_y = payload->doc_y,
					      doc_x = payload->doc_x]() {
				auto refs = project_manager::get_instance().lsp_query_references(fn, doc_y, doc_x);
				if (!refs.empty()) {
					editor_event oev;
					oev.type = event_type::open_file;
					oev.payload = std::format("{}:{}", refs[0].path, refs[0].range.start_y + 1);
					global_queue_.push(oev);
				}
			};
			new_items.push_back(ref_item);

			popup_menu_item fmt_item{static_cast<int>(event_type::format_doc), "Format Code", 'F', false};
			new_items.push_back(fmt_item);
		}

		active_popup_->set_items(new_items);
		global_queue_.push(event_type::redraw);
		return;
	}
}
