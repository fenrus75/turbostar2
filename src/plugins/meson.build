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

securityagent_plugin = shared_module('securityagent',
  'securityagent/plugin.cpp',
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

