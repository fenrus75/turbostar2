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
   'asm86/x86_disassemble/x86_disassemble_security.cpp'],
  dependencies: [zydis_dep, nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

