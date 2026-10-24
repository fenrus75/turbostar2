// Tested source file: src/call_synthesizer.cpp
#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include <memory>
#include "call_synthesizer.h"
#include "document.h"
#include "event_queue.h"

using namespace turbostar;

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Starting test_call_synthesizer..." << std::endl;

	// 1. Test signature parsing
	{
		auto sig_opt = call_synthesizer::parse_function_signature("void draw_border(int x, int y, int width, int height, int style, int pair = 1)");
		assert(sig_opt.has_value());
		const auto &sig = *sig_opt;
		assert(sig.function_name == "draw_border");
		assert(sig.parameters.size() == 6);
		assert(sig.parameters[0].name == "x" && sig.parameters[0].type == "int");
		assert(sig.parameters[1].name == "y" && sig.parameters[1].type == "int");
		assert(sig.parameters[2].name == "width" && sig.parameters[2].type == "int");
		assert(sig.parameters[3].name == "height" && sig.parameters[3].type == "int");
		assert(sig.parameters[4].name == "style" && sig.parameters[4].type == "int");
		assert(sig.parameters[5].name == "pair" && sig.parameters[5].default_value == "1");
	}

	// 2. Test signature parsing with templates
	{
		auto sig_opt = call_synthesizer::parse_function_signature("bool process(const std::vector<std::string, std::allocator<std::string>> &items, int count)");
		assert(sig_opt.has_value());
		const auto &sig = *sig_opt;
		assert(sig.function_name == "process");
		assert(sig.parameters.size() == 2);
		assert(sig.parameters[0].type == "const std::vector<std::string, std::allocator<std::string>> &");
		assert(sig.parameters[0].name == "items");
		assert(sig.parameters[1].type == "int");
		assert(sig.parameters[1].name == "count");
	}

	// 3. Test empty params signature
	{
		auto sig_opt = call_synthesizer::parse_function_signature("void clear()");
		assert(sig_opt.has_value());
		assert(sig_opt->function_name == "clear");
		assert(sig_opt->parameters.empty());

		auto suggestions = call_synthesizer::generate_argument_suggestions(*sig_opt, {});
		assert(!suggestions.empty());
		assert(suggestions[0].call_text == "()");
	}

	// 4. Test variable match scoring
	{
		variable_info v1{"x_", "int", 10};
		assert(call_synthesizer::score_variable_match("x", "int", v1) >= 130);

		variable_info v2{"filepath", "std::string", 12};
		assert(call_synthesizer::score_variable_match("path", "const std::string &", v2) >= 100);

		variable_info v3{"unrelated", "float", 14};
		assert(call_synthesizer::score_variable_match("width", "int", v3) == 0);
	}

	// 5. Test argument suggestion generation matching local variables
	{
		auto sig_opt = call_synthesizer::parse_function_signature("void draw_border(int x, int y, int width, int height, int pair = 1)");
		assert(sig_opt.has_value());

		std::vector<variable_info> locals = {
			{"x_", "int", 1},
			{"y_", "int", 2},
			{"width_", "int", 3},
			{"height_", "int", 4},
			{"border_pair", "int", 5}
		};

		auto suggestions = call_synthesizer::generate_argument_suggestions(*sig_opt, locals);
		assert(!suggestions.empty());
		// Best match should match x_, y_, width_, height_, border_pair
		assert(suggestions[0].call_text == "(x_, y_, width_, height_, border_pair)");
	}

	// 6. Test argument suggestion generation with existing argument bias
	{
		auto sig_opt = call_synthesizer::parse_function_signature("void set_bounds(int x, int y, int width, int height)");
		assert(sig_opt.has_value());

		std::vector<variable_info> locals = {
			{"width_", "int", 3},
			{"height_", "int", 4}
		};

		// User already specified 0, 1 for x and y
		auto suggestions = call_synthesizer::generate_argument_suggestions(*sig_opt, locals, "0, 1");
		assert(!suggestions.empty());
		assert(suggestions[0].call_text == "(0, 1, width_, height_)");
	}

	// 7. Test in-scope variable extraction from document
	{
		event_queue q;
		document doc(q);
		doc.insert_text(R"(
void window::render()
{
	int x_ = 0;
	int y_ = 1;
	int width_ = 80;
	int height_ = 24;
	std::string title = "Test";

	draw_border();
}
)");

		auto vars = call_synthesizer::extract_in_scope_variables(doc, 8);
		assert(!vars.empty());
		bool found_x = false;
		bool found_title = false;
		for (const auto &v : vars) {
			if (v.name == "x_") found_x = true;
			if (v.name == "title") found_title = true;
		}
		assert(found_x);
		assert(found_title);
	}

	std::cout << "All test_call_synthesizer assertions passed!" << std::endl;
	return 0;
}
