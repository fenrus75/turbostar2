#include "languages/cpp/language.h"
#include <filesystem>
#include "utf8.h"

namespace languages::cpp
{

bool supports_file(std::string_view filename) noexcept
{
	if (filename.empty()) {
		return false;
	}
	std::filesystem::path p(filename);
	std::string ext = utf8::to_lower(p.extension().string());
	return ext == ".cpp" || ext == ".h" || ext == ".hpp" || ext == ".c" || ext == ".cc" || ext == ".cxx";
}

bool supports_language(std::string_view lang) noexcept
{
	std::string lower_lang = utf8::to_lower(lang);
	return lower_lang == "c" || lower_lang == "cpp" || lower_lang == "c++" || lower_lang == "cc" || lower_lang == "h" ||
	       lower_lang == "hpp" || lower_lang == "cxx";
}

} // namespace languages::cpp
