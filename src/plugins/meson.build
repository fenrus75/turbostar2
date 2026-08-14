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
   'hexedit/hexwrite_validator.cpp',
   'hexedit/hexinspect_tool.cpp',
   'hexedit/hexinspect_validator.cpp'],
  dependencies: [nlohmann_json_dep, magic_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

if lexbor_dep.found()
  html_plugin = shared_module('html',
    ['html/plugin.cpp',
     'html/html_extract_tables_tool.cpp',
     'html/html_extract_tables_validator.cpp',
     'html/html_list_links_tool.cpp',
     'html/html_list_links_validator.cpp',
     'html/html_list_images_tool.cpp',
     'html/html_list_images_validator.cpp',
     'html/html_extract_text_tool.cpp',
     'html/html_extract_text_validator.cpp'],
    dependencies: [nlohmann_json_dep, lexbor_dep],
    include_directories: inc,
    install: true,
    name_prefix: '',
    install_dir: plugin_dir
  )
endif

mattpocock_plugin = shared_module('mattpocock',
  'mattpocock/plugin.cpp',
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

if graphicsmagick_dep.found()
  image_basic_plugin = shared_module('image_basic',
    ['image_basic/plugin.cpp',
     'image_basic/image_resize_tool.cpp',
     'image_basic/image_resize_validator.cpp',
     'image_basic/image_crop_tool.cpp',
     'image_basic/image_crop_validator.cpp',
     'image_basic/image_rotate_tool.cpp',
     'image_basic/image_rotate_validator.cpp',
     'image_basic/image_mirror_tool.cpp',
     'image_basic/image_mirror_validator.cpp',
     'image_basic/image_grayscale_tool.cpp',
     'image_basic/image_grayscale_validator.cpp',
     'image_basic/image_threshold_tool.cpp',
     'image_basic/image_threshold_validator.cpp',
     'image_basic/image_import_entry.cpp',
     'image_basic/image_import_security.cpp',
     'image_basic/image_export_entry.cpp',
     'image_basic/image_export_security.cpp',
     'image_basic/image_compose_tool.cpp',
     'image_basic/image_compose_validator.cpp',
     'image_basic/image_getdata_tool.cpp',
     'image_basic/image_getdata_validator.cpp'],
    dependencies: [nlohmann_json_dep, graphicsmagick_dep, curl_dep],
    include_directories: inc,
    install: true,
    name_prefix: '',
    install_dir: plugin_dir
  )
endif

binary_plugin = shared_module('binary',
  ['binary/plugin.cpp',
   'binary/data_compress_tool.cpp',
   'binary/data_compress_validator.cpp',
   'binary/data_decompress_tool.cpp',
   'binary/data_decompress_validator.cpp',
   'binary/binary_utils.cpp'],
  dependencies: [nlohmann_json_dep, curl_dep, zlib_dep, liblzma_dep, libzstd_dep, bzip2_dep, liblz4_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

a2acardgenerator_agent_h = custom_target('a2acardgenerator_agent.h',
  input : 'a2a/a2acardgenerator.md',
  output : 'a2acardgenerator_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'a2acardgenerator_agent_md']
)

a2a_plugin = shared_module('a2a',
  ['a2a/plugin.cpp',
   'a2a/a2a_validate_card/a2a_validate_card_entry.cpp',
   'a2a/a2a_validate_card/a2a_validate_card_security.cpp',
   'a2a/a2a_generate_card_with_agent/a2a_generate_card_with_agent_entry.cpp',
   'a2a/a2a_generate_card_with_agent/a2a_generate_card_with_agent_security.cpp',
   a2acardgenerator_agent_h],
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)

if sqlite3_dep.found()
  sqlite_plugin = shared_module('sqlite',
    ['sqlite/plugin.cpp',
     'sqlite/sqlite_create_db_entry.cpp',
     'sqlite/sqlite_create_db_security.cpp',
     'sqlite/sqlite_delete_db_entry.cpp',
     'sqlite/sqlite_delete_db_security.cpp',
     'sqlite/sqlite_list_db_entry.cpp',
     'sqlite/sqlite_list_db_security.cpp',
     'sqlite/sqlite_perform_entry.cpp',
     'sqlite/sqlite_perform_security.cpp'],
    dependencies: [nlohmann_json_dep, sqlite3_dep],
    include_directories: inc,
    install: true,
    name_prefix: '',
    install_dir: plugin_dir
  )
endif

markdown_extract_agent_h = custom_target('markdown_extract_agent.h',
  input : 'markdown_extract/markdown_extract_agent.md',
  output : 'markdown_extract_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'markdown_extract_agent_md']
)

markdown_extract_plugin = shared_module('markdown_extract',
  ['markdown_extract/plugin.cpp',
   'markdown_extract/markdown_extract_tool.cpp',
   'markdown_extract/markdown_extract_validator.cpp',
   markdown_extract_agent_h],
  dependencies: [nlohmann_json_dep],
  include_directories: inc,
  install: true,
  name_prefix: '',
  install_dir: plugin_dir
)


