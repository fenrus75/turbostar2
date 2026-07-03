dummy_plugin = shared_module('dummy',
  'dummy/plugin.cpp',
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

asm86_plugin = shared_module('asm86',
  ['asm86/plugin.cpp',
   'asm86/x86_assemble/x86_assemble_entry.cpp',
   'asm86/x86_assemble/x86_assemble_security.cpp',
   'asm86/x86_disassemble/x86_disassemble_entry.cpp',
   'asm86/x86_disassemble/x86_disassemble_security.cpp',
   'asm86/elf_list_sections/elf_list_sections_entry.cpp',
   'asm86/elf_list_sections/elf_list_sections_security.cpp',
   'asm86/elf_list_symbols/elf_list_symbols_entry.cpp',
   'asm86/elf_list_symbols/elf_list_symbols_security.cpp'],
  dependencies: [zydis_dep, nlohmann_json_dep, re2_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

security_movie_h = custom_target('security_movie.h',
  input : 'securityagent/security.json',
  output : 'security_movie.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'security_movie_json']
)

securityagent_agent_h = custom_target('securityagent_agent.h',
  input : 'securityagent/securityagent.md',
  output : 'securityagent_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'securityagent_agent_md']
)

securityagent_plugin = shared_module('securityagent',
  ['securityagent/plugin.cpp',
   'securityagent/security_scan_python/security_scan_python_entry.cpp',
   'securityagent/security_scan_python/security_scan_python_security.cpp',
   'securityagent/security_scan_c/security_scan_c_entry.cpp',
   'securityagent/security_scan_c/security_scan_c_security.cpp',
   'securityagent/security_scan_semgrep/security_scan_semgrep_entry.cpp',
   'securityagent/security_scan_semgrep/security_scan_semgrep_security.cpp',
   'securityagent/security_verify_html/security_verify_html_entry.cpp',
   'securityagent/security_verify_html/security_verify_html_security.cpp',
   'securityagent/security_review_with_agent/security_review_with_agent_entry.cpp',
   'securityagent/security_review_with_agent/security_review_with_agent_security.cpp',
   security_movie_h,
   securityagent_agent_h],
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

hexedit_plugin = shared_module('hexedit',
  ['hexedit/plugin.cpp',
   'hexedit/hexdump_tool.cpp',
   'hexedit/hexdump_validator.cpp',
   'hexedit/hexwrite_tool.cpp',
   'hexedit/hexwrite_validator.cpp'],
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

