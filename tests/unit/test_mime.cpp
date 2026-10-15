// Tested source file: src/mime.cpp
#include "test_watchdog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include "mime.h"

void test_mime_from_extension()
{
	// Image formats
	assert(mime::from_extension("file.png") == "image/png");
	assert(mime::from_extension("IMAGE.PNG") == "image/png");
	assert(mime::from_extension("photo.jpg") == "image/jpeg");
	assert(mime::from_extension("photo.jpeg") == "image/jpeg");
	assert(mime::from_extension("PHOTO.JPEG") == "image/jpeg");
	assert(mime::from_extension("anim.gif") == "image/gif");
	assert(mime::from_extension("ANIM.GIF") == "image/gif");
	assert(mime::from_extension("graphic.webp") == "image/webp");
	assert(mime::from_extension("GRAPHIC.WEBP") == "image/webp");

	// Documents and text formats
	assert(mime::from_extension("doc.pdf") == "application/pdf");
	assert(mime::from_extension("DOC.PDF") == "application/pdf");
	assert(mime::from_extension("file.txt") == "text/plain");
	assert(mime::from_extension("FILE.TXT") == "text/plain");
	assert(mime::from_extension("index.html") == "text/html");
	assert(mime::from_extension("index.htm") == "text/html");
	assert(mime::from_extension("INDEX.HTML") == "text/html");
	assert(mime::from_extension("style.css") == "text/css");
	assert(mime::from_extension("app.js") == "text/javascript");
	assert(mime::from_extension("data.json") == "application/json");
	assert(mime::from_extension("feed.xml") == "application/xml");

	// Archives
	assert(mime::from_extension("archive.zip") == "application/zip");
	assert(mime::from_extension("archive.tar") == "application/x-tar");
	assert(mime::from_extension("bundle.gz") == "application/gzip");

	// Audio / Video
	assert(mime::from_extension("song.mp3") == "audio/mpeg");
	assert(mime::from_extension("movie.mp4") == "video/mp4");
	assert(mime::from_extension("sound.wav") == "audio/wav");
	assert(mime::from_extension("audio.ogg") == "audio/ogg");

	// Bare extension without dot
	assert(mime::from_extension("png") == "image/png");
	assert(mime::from_extension("PNG") == "image/png");
	assert(mime::from_extension("pdf") == "application/pdf");
	assert(mime::from_extension("json") == "application/json");

	// Multi-dot filenames
	assert(mime::from_extension("archive.tar.gz") == "application/gzip");
	assert(mime::from_extension("bundle.min.js") == "text/javascript");
	assert(mime::from_extension("theme.min.css") == "text/css");

	// Full file paths
	assert(mime::from_extension("/usr/local/share/doc.pdf") == "application/pdf");
	assert(mime::from_extension("C:\\Users\\User\\photo.jpg") == "image/jpeg");

	// Fallback to application/octet-stream
	assert(mime::from_extension("") == "application/octet-stream");
	assert(mime::from_extension("Makefile") == "application/octet-stream");
	assert(mime::from_extension("file.") == "application/octet-stream");
	assert(mime::from_extension("file.unknown_extension") == "application/octet-stream");
	assert(mime::from_extension("binary_executable") == "application/octet-stream");
}

void test_mime_get_language_from_extension()
{
	// C and C++ variants
	assert(mime::get_language_from_extension("src/main.cpp") == "cpp");
	assert(mime::get_language_from_extension("source.cc") == "cpp");
	assert(mime::get_language_from_extension("module.cxx") == "cpp");
	assert(mime::get_language_from_extension("driver.c") == "cpp");
	assert(mime::get_language_from_extension("header.h") == "cpp");
	assert(mime::get_language_from_extension("impl.hpp") == "cpp");
	assert(mime::get_language_from_extension("types.hh") == "cpp");
	assert(mime::get_language_from_extension("defs.hxx") == "cpp");
	assert(mime::get_language_from_extension("MAIN.CPP") == "cpp");
	assert(mime::get_language_from_extension("HEADER.H") == "cpp");

	// Python variants
	assert(mime::get_language_from_extension(".py") == "python");
	assert(mime::get_language_from_extension("script.py") == "python");
	assert(mime::get_language_from_extension("gui.pyw") == "python");
	assert(mime::get_language_from_extension("types.pyi") == "python");
	assert(mime::get_language_from_extension("SCRIPT.PY") == "python");

	// C#
	assert(mime::get_language_from_extension("Program.cs") == "csharp");
	assert(mime::get_language_from_extension(".cs") == "csharp");
	assert(mime::get_language_from_extension("cs") == "csharp");

	// JavaScript / TypeScript variants
	assert(mime::get_language_from_extension("app.js") == "javascript");
	assert(mime::get_language_from_extension("app.ts") == "javascript");
	assert(mime::get_language_from_extension("comp.jsx") == "javascript");
	assert(mime::get_language_from_extension("comp.tsx") == "javascript");
	assert(mime::get_language_from_extension("lib.cjs") == "javascript");
	assert(mime::get_language_from_extension("mod.mjs") == "javascript");

	// Rust and Go
	assert(mime::get_language_from_extension("main.rs") == "rust");
	assert(mime::get_language_from_extension("server.go") == "go");

	// Other languages
	assert(mime::get_language_from_extension("App.java") == "java");
	assert(mime::get_language_from_extension("script.rb") == "ruby");
	assert(mime::get_language_from_extension("index.php") == "php");
	assert(mime::get_language_from_extension("query.sql") == "sql");
	assert(mime::get_language_from_extension("build.sh") == "bash");
	assert(mime::get_language_from_extension("run.bash") == "bash");
	assert(mime::get_language_from_extension("env.zsh") == "bash");
	assert(mime::get_language_from_extension("script.lua") == "lua");

	// Web formats
	assert(mime::get_language_from_extension("index.html") == "html");
	assert(mime::get_language_from_extension("index.htm") == "html");
	assert(mime::get_language_from_extension("style.css") == "css");
	assert(mime::get_language_from_extension("theme.scss") == "css");
	assert(mime::get_language_from_extension("theme.sass") == "css");
	assert(mime::get_language_from_extension("theme.less") == "css");

	// Data formats
	assert(mime::get_language_from_extension("data.json") == "json");
	assert(mime::get_language_from_extension("config.jsonc") == "json");
	assert(mime::get_language_from_extension("config.yaml") == "yaml");
	assert(mime::get_language_from_extension("config.yml") == "yaml");
	assert(mime::get_language_from_extension("doc.xml") == "xml");
	assert(mime::get_language_from_extension("icon.svg") == "xml");
	assert(mime::get_language_from_extension("config.toml") == "ini");
	assert(mime::get_language_from_extension("settings.ini") == "ini");
	assert(mime::get_language_from_extension("server.conf") == "ini");

	// Documentation
	assert(mime::get_language_from_extension("README.md") == "markdown");
	assert(mime::get_language_from_extension("guide.markdown") == "markdown");

	// Hardware description languages
	assert(mime::get_language_from_extension("counter.v") == "verilog");
	assert(mime::get_language_from_extension("header.vh") == "verilog");
	assert(mime::get_language_from_extension("cpu.sv") == "systemverilog");
	assert(mime::get_language_from_extension("bus.svh") == "systemverilog");

	// Bare extension strings without dot
	assert(mime::get_language_from_extension("cpp") == "cpp");
	assert(mime::get_language_from_extension("py") == "python");
	assert(mime::get_language_from_extension("rs") == "rust");
	assert(mime::get_language_from_extension("go") == "go");
	assert(mime::get_language_from_extension("json") == "json");

	// Unknown or empty
	assert(mime::get_language_from_extension("") == "");
	assert(mime::get_language_from_extension("Makefile") == "");
	assert(mime::get_language_from_extension("Dockerfile") == "");
	assert(mime::get_language_from_extension("unknown_file.xyz") == "");
}

void test_mime_uses_brace_syntax()
{
	// Positive cases (languages using curly braces for blocks)
	// C / C++
	assert(mime::uses_brace_syntax("main.cpp"));
	assert(mime::uses_brace_syntax("header.h"));
	assert(mime::uses_brace_syntax("code.c"));
	assert(mime::uses_brace_syntax("impl.hpp"));
	assert(mime::uses_brace_syntax("source.cxx"));
	assert(mime::uses_brace_syntax("cpp"));
	assert(mime::uses_brace_syntax(".cpp"));
	assert(mime::uses_brace_syntax("MAIN.CPP"));

	// C#
	assert(mime::uses_brace_syntax("Program.cs"));
	assert(mime::uses_brace_syntax(".cs"));
	assert(mime::uses_brace_syntax("cs"));
	assert(mime::uses_brace_syntax("PROGRAM.CS"));

	// JavaScript / TypeScript
	assert(mime::uses_brace_syntax("app.js"));
	assert(mime::uses_brace_syntax("index.ts"));
	assert(mime::uses_brace_syntax("comp.jsx"));
	assert(mime::uses_brace_syntax("view.tsx"));
	assert(mime::uses_brace_syntax("lib.cjs"));
	assert(mime::uses_brace_syntax("mod.mjs"));
	assert(mime::uses_brace_syntax("js"));
	assert(mime::uses_brace_syntax("ts"));

	// Rust and Go
	assert(mime::uses_brace_syntax("main.rs"));
	assert(mime::uses_brace_syntax("rs"));
	assert(mime::uses_brace_syntax("server.go"));
	assert(mime::uses_brace_syntax("go"));

	// Java, Kotlin, Swift, Scala, PHP, D
	assert(mime::uses_brace_syntax("App.java"));
	assert(mime::uses_brace_syntax("java"));
	assert(mime::uses_brace_syntax("Main.kt"));
	assert(mime::uses_brace_syntax("build.kts"));
	assert(mime::uses_brace_syntax("kt"));
	assert(mime::uses_brace_syntax("App.swift"));
	assert(mime::uses_brace_syntax("swift"));
	assert(mime::uses_brace_syntax("Main.scala"));
	assert(mime::uses_brace_syntax("scala"));
	assert(mime::uses_brace_syntax("index.php"));
	assert(mime::uses_brace_syntax("php"));
	assert(mime::uses_brace_syntax("app.d"));
	assert(mime::uses_brace_syntax("d"));

	// Shaders and Hardware Description
	assert(mime::uses_brace_syntax("shader.glsl"));
	assert(mime::uses_brace_syntax("vertex.hlsl"));
	assert(mime::uses_brace_syntax("module.v"));
	assert(mime::uses_brace_syntax("header.vh"));
	assert(mime::uses_brace_syntax("top.sv"));
	assert(mime::uses_brace_syntax("top.svh"));

	// CSS, JSON, XML, HTML
	assert(mime::uses_brace_syntax("style.css"));
	assert(mime::uses_brace_syntax("theme.scss"));
	assert(mime::uses_brace_syntax("data.json"));
	assert(mime::uses_brace_syntax("config.jsonc"));
	assert(mime::uses_brace_syntax("doc.xml"));
	assert(mime::uses_brace_syntax("icon.svg"));
	assert(mime::uses_brace_syntax("page.html"));

	// Negative cases (indentation-based, keyword-based, or non-code)
	assert(!mime::uses_brace_syntax("script.py"));
	assert(!mime::uses_brace_syntax(".py"));
	assert(!mime::uses_brace_syntax("py"));
	assert(!mime::uses_brace_syntax("helper.pyw"));
	assert(!mime::uses_brace_syntax("build.sh"));
	assert(!mime::uses_brace_syntax("run.bash"));
	assert(!mime::uses_brace_syntax("script.rb"));
	assert(!mime::uses_brace_syntax("config.yaml"));
	assert(!mime::uses_brace_syntax("deploy.yml"));
	assert(!mime::uses_brace_syntax("config.toml"));
	assert(!mime::uses_brace_syntax("settings.ini"));
	assert(!mime::uses_brace_syntax("README.md"));
	assert(!mime::uses_brace_syntax("notes.txt"));
	assert(!mime::uses_brace_syntax("Makefile"));
	assert(!mime::uses_brace_syntax(""));
	assert(!mime::uses_brace_syntax("unknown.xyz"));
}

void test_mime_detect_buffer_type()
{
	using namespace std::string_literals;

	// Image magic bytes (valid header sequences)
	std::string png_buf = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"s;
	assert(mime::detect_buffer_type(png_buf) == "image/png");

	std::string jpeg_buf = "\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x01\x00\x60\x00\x60\x00\x00"s;
	assert(mime::detect_buffer_type(jpeg_buf) == "image/jpeg");

	std::string gif_buf89 = "GIF89a\x01\x00\x01\x00\x80\x00\x00\xff\xff\xff\x00\x00\x00!\xf9\x04\x01\x00\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;"s;
	assert(mime::detect_buffer_type(gif_buf89) == "image/gif");

	std::string gif_buf87 = "GIF87a\x01\x00\x01\x00\x80\x00\x00\xff\xff\xff\x00\x00\x00,\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02D\x01\x00;"s;
	assert(mime::detect_buffer_type(gif_buf87) == "image/gif");

	// Empty and short buffers
#ifdef HAS_LIBMAGIC
	std::string empty_mime = mime::detect_buffer_type("");
	assert(empty_mime == "application/x-empty" || empty_mime == "inode/x-empty");
	assert(mime::detect_buffer_type("abc") == "text/plain");
#else
	assert(mime::detect_buffer_type("") == "application/octet-stream");
	assert(mime::detect_buffer_type("abc") == "application/octet-stream");
#endif

	// Unknown binary payload
	std::string rand_bin = "\x01\x02\x03\x04\x05\x06\x07\x08"s;
	assert(mime::detect_buffer_type(rand_bin) == "application/octet-stream");

#ifdef HAS_LIBMAGIC
	// Plain text buffer via libmagic
	std::string text_buf = "Hello, this is a plain text file content without binary bytes.\n";
	std::string text_mime = mime::detect_buffer_type(text_buf);
	assert(text_mime.find("text") != std::string::npos);

	// PDF buffer via libmagic
	std::string pdf_buf = "%PDF-1.4\n%âãÏÓ\n1 0 obj\n<<>>\nendobj\n";
	assert(mime::detect_buffer_type(pdf_buf) == "application/pdf");
#endif
}

void test_mime_detect_buffer_description()
{
	using namespace std::string_literals;

	// Empty buffer
	assert(!mime::detect_buffer_description("").empty());

	// Short buffer
	assert(!mime::detect_buffer_description("short").empty());

#ifdef HAS_LIBMAGIC
	// Image buffer description
	std::string png_buf = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"s;
	std::string png_desc = mime::detect_buffer_description(png_buf);
	assert(png_desc.find("PNG") != std::string::npos || png_desc.find("image") != std::string::npos);

	// PDF buffer description
	std::string pdf_buf = "%PDF-1.4\n1 0 obj\n<<>>\nendobj\n";
	std::string pdf_desc = mime::detect_buffer_description(pdf_buf);
	assert(pdf_desc.find("PDF") != std::string::npos);
#endif
}

void test_mime_detect_file_type()
{
	// 1. Existing repository files
	// PDF file
	std::string pdf_path = "tests/shared-mime-info-spec.pdf";
	if (!std::filesystem::exists(pdf_path) && std::filesystem::exists("../tests/shared-mime-info-spec.pdf")) {
		pdf_path = "../tests/shared-mime-info-spec.pdf";
	}
	assert(std::filesystem::exists(pdf_path));
	assert(mime::detect_file_type(pdf_path) == "application/pdf");

	// C++ source file
	std::string cpp_path = "src/main.cpp";
	if (!std::filesystem::exists(cpp_path) && std::filesystem::exists("../src/main.cpp")) {
		cpp_path = "../src/main.cpp";
	}
	assert(std::filesystem::exists(cpp_path));
	std::string main_mime = mime::detect_file_type(cpp_path);
	assert(!main_mime.empty());
	assert(main_mime.find("text") != std::string::npos || main_mime.find("c") != std::string::npos);
	// 2. Non-existent files with known extension fallback
	assert(mime::detect_file_type("nonexistent_image.png") == "image/png");
	assert(mime::detect_file_type("nonexistent_doc.pdf") == "application/pdf");
	assert(mime::detect_file_type("nonexistent_data.json") == "application/json");

	// 3. URI schemes bypass filesystem inspection
	assert(mime::detect_file_type("file:///workspace/test.txt") == "text/plain");
	assert(mime::detect_file_type("http://example.com/photo.jpeg") == "image/jpeg");
	assert(mime::detect_file_type("https://example.com/asset.webp") == "image/webp");

	// 4. File without extension and no file on disk
	assert(mime::detect_file_type("nonexistent_binary_file") == "application/octet-stream");

	// 5. Temporary files with signature bytes
	using namespace std::string_view_literals;
	std::filesystem::path tmp_png = std::filesystem::temp_directory_path() / "test_mime_temp_signature.tmp";
	{
		std::ofstream out(tmp_png, std::ios::binary);
		std::string_view png_header = "\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89"sv;
		out.write(png_header.data(), png_header.size());
	}
	assert(mime::detect_file_type(tmp_png.string()) == "image/png");
	std::filesystem::remove(tmp_png);

	std::filesystem::path tmp_jpeg = std::filesystem::temp_directory_path() / "test_mime_temp_signature_jpeg.tmp";
	{
		std::ofstream out(tmp_jpeg, std::ios::binary);
		std::string_view jpeg_header = "\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x01\x00\x60\x00\x60\x00\x00"sv;
		out.write(jpeg_header.data(), jpeg_header.size());
	}
	assert(mime::detect_file_type(tmp_jpeg.string()) == "image/jpeg");
	std::filesystem::remove(tmp_jpeg);
}

void test_mime_detect_file_description()
{
	// Non-existent file
	assert(mime::detect_file_description("nonexistent_file_path.xyz") == "Unknown file type");

	// Existing PDF file in repository
	std::string pdf_path = "tests/shared-mime-info-spec.pdf";
	if (!std::filesystem::exists(pdf_path) && std::filesystem::exists("../tests/shared-mime-info-spec.pdf")) {
		pdf_path = "../tests/shared-mime-info-spec.pdf";
	}
	assert(std::filesystem::exists(pdf_path));
	std::string pdf_desc = mime::detect_file_description(pdf_path);
#ifdef HAS_LIBMAGIC
	assert(pdf_desc.find("PDF") != std::string::npos);
#else
	assert(!pdf_desc.empty());
#endif

	// Existing C++ source file in repository
	std::string cpp_path = "src/main.cpp";
	if (!std::filesystem::exists(cpp_path) && std::filesystem::exists("../src/main.cpp")) {
		cpp_path = "../src/main.cpp";
	}
	assert(std::filesystem::exists(cpp_path));
	std::string cpp_desc = mime::detect_file_description(cpp_path);
	assert(!cpp_desc.empty());
}

int main()
{
	test_watchdog::setup_watchdog(30);

	test_mime_from_extension();
	test_mime_get_language_from_extension();
	test_mime_uses_brace_syntax();
	test_mime_detect_buffer_type();
	test_mime_detect_buffer_description();
	test_mime_detect_file_type();
	test_mime_detect_file_description();

	std::cout << "All MIME unit tests passed successfully!" << std::endl;
	return 0;
}
