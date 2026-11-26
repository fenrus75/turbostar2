// Tested source file: src/languages/language_settings.cpp
#include "config_manager.h"
#include "languages/language_settings.h"
#include "test_watchdog.h"

#include <cassert>
#include <cmath>
#include <iostream>

int main()
{
	test_watchdog::setup_watchdog(30);

	// SystemVerilog / Verilog -> 0.1
	auto sv_temp = languages::language_settings::suggested_temperature("SystemVerilog");
	assert(sv_temp.has_value());
	assert(std::fabs(*sv_temp - 0.1) < 1e-9);

	auto sv_lower = languages::language_settings::suggested_temperature("systemverilog");
	assert(sv_lower.has_value());
	assert(std::fabs(*sv_lower - 0.1) < 1e-9);

	auto v_temp = languages::language_settings::suggested_temperature("Verilog");
	assert(v_temp.has_value());
	assert(std::fabs(*v_temp - 0.1) < 1e-9);

	// Other languages -> std::nullopt (use model/provider default)
	assert(!languages::language_settings::suggested_temperature("C++").has_value());
	assert(!languages::language_settings::suggested_temperature("C").has_value());
	assert(!languages::language_settings::suggested_temperature("Python").has_value());
	assert(!languages::language_settings::suggested_temperature("Rust").has_value());
	assert(!languages::language_settings::suggested_temperature("Other").has_value());
	assert(!languages::language_settings::suggested_temperature("").has_value());

	// Honors config_manager primary language setting
	auto &cfg = config_manager::get_instance();
	std::string orig_lang = cfg.get_primary_language();

	cfg.set_primary_language("C++");
	assert(!languages::language_settings::suggested_temperature_for_current_project().has_value());

	cfg.set_primary_language("SystemVerilog");
	auto cur_temp = languages::language_settings::suggested_temperature_for_current_project();
	assert(cur_temp.has_value());
	assert(std::fabs(*cur_temp - 0.1) < 1e-9);

	cfg.set_primary_language(orig_lang);

	// Reasoning model guard
	assert(!languages::language_settings::model_supports_temperature("o1"));
	assert(!languages::language_settings::model_supports_temperature("o1-mini"));
	assert(!languages::language_settings::model_supports_temperature("o3-mini"));
	assert(!languages::language_settings::model_supports_temperature("o4-mini"));
	assert(!languages::language_settings::model_supports_temperature("openai/o3"));
	assert(languages::language_settings::model_supports_temperature("gpt-4.1"));
	assert(languages::language_settings::model_supports_temperature("gpt-4o"));
	assert(languages::language_settings::model_supports_temperature("qwen3-coder"));

	std::cout << "PASS: test_language_settings\n";
	return 0;
}
