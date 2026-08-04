#include "test_watchdog.h"
#include <cassert>
#include <iostream>
#include "mime.h"

void test_mime_get_language_from_extension()
{
	assert(mime::get_language_from_extension("src/main.cpp") == "cpp");
	assert(mime::get_language_from_extension("header.h") == "cpp");
	assert(mime::get_language_from_extension("impl.hpp") == "cpp");
	assert(mime::get_language_from_extension(".py") == "python");
	assert(mime::get_language_from_extension("script.py") == "python");
	assert(mime::get_language_from_extension("data.json") == "json");
	assert(mime::get_language_from_extension("README.md") == "markdown");
	assert(mime::get_language_from_extension("build.sh") == "bash");
	assert(mime::get_language_from_extension("app.ts") == "javascript");
	assert(mime::get_language_from_extension("index.html") == "html");
	assert(mime::get_language_from_extension("style.css") == "css");
	assert(mime::get_language_from_extension("config.yaml") == "yaml");
	assert(mime::get_language_from_extension("main.rs") == "rust");
	assert(mime::get_language_from_extension("server.go") == "go");
	assert(mime::get_language_from_extension("unknown_file.xyz") == "");
}

void test_mime_from_extension()
{
	assert(mime::from_extension("file.png") == "image/png");
	assert(mime::from_extension("doc.pdf") == "application/pdf");
	assert(mime::from_extension("file.txt") == "text/plain");
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_mime_get_language_from_extension();
	test_mime_from_extension();

	std::cout << "All MIME unit tests passed successfully!" << std::endl;
	return 0;
}
