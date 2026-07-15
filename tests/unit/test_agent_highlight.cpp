#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "agentlib/interactions/llm_response.h"
#include "syntax_color_manager.h"

using namespace agentlib;

int main()
{
	test_watchdog::setup_watchdog(30);
	std::cout << "Running test_agent_highlight..." << std::endl;

	// Initialize the syntax color manager
	syntax_color_manager::get_instance().initialize();

	// Define a markdown text containing a C++ code block
	std::string text = 
		"Here is some code:\n"
		"```cpp\n"
		"int main() {\n"
		"    // This is a comment\n"
		"    return 0;\n"
		"}\n"
		"```\n"
		"And that's it.";

	interaction_llm_response response(text);
	auto lines = response.render(80, background_mode::light_blue);

	// Let's print out the wrapped lines and their character colors to verify
	bool found_comment = false;
	bool found_keyword = false;
	bool found_normal = false;

	for (const auto &line : lines) {
		std::cout << "Line: [" << line.text << "], color_pair=" << line.color_pair << std::endl;
		if (!line.char_color_pairs.empty()) {
			std::cout << "  Char colors: ";
			for (size_t i = 0; i < line.char_color_pairs.size(); ++i) {
				std::cout << line.char_color_pairs[i] << " ";
			}
			std::cout << std::endl;

			// Verify that the comment line uses the comment color pair
			if (line.text.find("// This is a comment") != std::string::npos) {
				found_comment = true;
				// The comment starts at index 4 (due to spaces)
				int comment_cp = syntax_color_manager::get_instance().get_color_pair(syntax_attribute::comment);
				assert(line.char_color_pairs[8] == comment_cp);
			}

			// Verify that "int" is colored as keyword
			if (line.text.find("int main()") != std::string::npos) {
				found_keyword = true;
				int kw_cp = syntax_color_manager::get_instance().get_color_pair(syntax_attribute::keyword);
				assert(line.char_color_pairs[0] == kw_cp); // 'i'
				assert(line.char_color_pairs[1] == kw_cp); // 'n'
				assert(line.char_color_pairs[2] == kw_cp); // 't'
				
				// ' ' and 'main' and '(' should be normal (or whatever color_pair defaults to in code blocks)
				int code_default_cp = 3;
				assert(line.char_color_pairs[3] == code_default_cp); // ' '
			}
		} else {
			if (line.text.find("Here is some code:") != std::string::npos) {
				found_normal = true;
			}
		}
	}

	assert(found_comment && "C++ comments should be highlighted");
	assert(found_keyword && "C++ keywords should be highlighted");
	assert(found_normal && "Normal text outside code blocks should not have character colors");

	std::cout << "test_agent_highlight passed successfully!" << std::endl;
	return 0;
}
