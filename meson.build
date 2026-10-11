# ==============================================================================
# TurboStar Build System Configuration
#
# Note on project file organization:
# This build file has been split into modular build definition files:
#  - src/meson.build: Core editor sources, agent libraries, and language backends.
#  - src/tools/meson.build: Native tool implementations and tool registration.
#  - tests/meson.build: All unit tests, E2E test suites, and test deployment targets
#    (included conditionally via subdir('tests') when -Denable-tests=true).
# ==============================================================================

project('turbostar', ['cpp', 'c'],
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

add_project_arguments('-DTURBOSTAR_VERSION="' + meson.project_version() + '"', language : 'cpp')
add_project_arguments('-DTURBOSTAR_CRASHPROCESS_PATH="' + get_option('prefix') + '/' + get_option('bindir') + '/turbostar-crashprocess"', language : 'cpp')
add_project_arguments('-DCPPHTTPLIB_OPENSSL_SUPPORT', language : 'cpp')

cc = meson.get_compiler('cpp')

# src/document_format.cpp formats a std::thread::id directly; libstdc++ 13 lacks P2693.
if not cc.compiles('''#include <version>
#if !defined(__cpp_lib_formatters) || __cpp_lib_formatters < 202302L
#error "std::formatter<std::thread::id> is unavailable"
#endif
''', name : 'std::formatter<std::thread::id> (P2693)')
  error('std::formatter<std::thread::id> (P2693) is required. Upgrade libstdc++.')
endif

# tests/unit detect AddressSanitizer via __has_feature, which GCC only added in 14.
if not cc.compiles('''#if defined(__has_feature) && __has_feature(address_sanitizer)
#endif
''', name : '__has_feature in preprocessor conditionals')
  error('__has_feature must be usable inside #if. Upgrade compiler.')
endif

if cc.get_id() == 'clang'
  add_project_arguments('-Wthread-safety', language : 'cpp')
endif

if get_option('enable-analyzer')
  if cc.get_id() == 'gcc' and cc.has_argument('-fanalyzer')
    add_project_arguments('-fanalyzer', language : 'cpp')
  endif
endif
git_prog = find_program('git', required: false)
git_version_h = vcs_tag(
  input : 'src/git_version.h.in',
  output : 'git_version.h',
  fallback : meson.project_version(),
  command : ['git', 'describe', '--always', '--dirty']
)
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)

endif

if not cc.has_header('dtl/dtl.hpp')
  error('dtl/dtl.hpp is required but not found. Please install libdtl-dev (or equivalent).')
endif

# In some versions of dtl, enableTrivial() const assigns to a non-mutable member. 
# That is ill-formed C++ that GCC 14+ rejects at parse time.
if not cc.compiles('#include <dtl/dtl.hpp>', name : 'dtl headers compile')
  error('dtl/dtl.hpp is rejected by current compiler. Install a newer version of dtl.')
endif

embed_prog = find_program('python3')
research_agent_h = custom_target('research_agent.h',
  input : 'agents/research.md',
  output : 'research_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'research_agent_md']
)

self_agent_h = custom_target('self_agent.h',
  input : 'agents/self.md',
  output : 'self_agent.h',
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_text.py'), '@INPUT@', '@OUTPUT@', 'self_agent_md']
)

system_docs_files = files(
  'system/languages/c17.md',
  'system/languages/cpp23.md',
  'system/languages/cpp23_short.md',
  'system/languages/default.md',
  'system/languages/default_short.md',
  'system/languages/python311.md',
  'system/languages/rust2021.md',
  'system/languages/typescript.md',
  'system/languages/verilog.md',
  'system/languages/systemverilog.md',
  'system/workflows/code_review.md',
  'system/workflows/crash_analysis.md',
  'system/workflows/bugfixing.md',
)

system_docs_embedded_h = custom_target('system_docs_embedded.h',
  output : 'system_docs_embedded.h',
  depend_files : system_docs_files,
  command : [embed_prog, join_paths(meson.project_source_root(), 'scripts/embed_system_docs.py'), join_paths(meson.project_source_root(), 'system'), '@OUTPUT@']
)

httplib_dep = dependency('cpp-httplib', required : true)
curl_dep = dependency('libcurl', required : true)
openssl_dep = dependency('openssl', required : true)
# 3.11 added the std::optional support the NLOHMANN_DEFINE_TYPE_* validators rely on.
nlohmann_json_dep = dependency('nlohmann_json', version : '>=3.11', required : true)
thread_dep = dependency('threads')
re2_dep = dependency('re2', required : true)
sqlite3_dep = dependency('sqlite3', required : true)
zlib_dep = dependency('zlib', required : true)
liblzma_dep = dependency('liblzma', required : false)
if liblzma_dep.found()
  add_project_arguments('-DHAS_LIBLZMA', language : 'cpp')
endif
libzstd_dep = dependency('libzstd', required : false)
if libzstd_dep.found()
  add_project_arguments('-DHAS_LIBZSTD', language : 'cpp')
endif
bzip2_dep = dependency('bzip2', required : false)
if bzip2_dep.found()
  add_project_arguments('-DHAS_BZIP2', language : 'cpp')
endif
liblz4_dep = dependency('liblz4', required : false)
if liblz4_dep.found()
  add_project_arguments('-DHAS_LIBLZ4', language : 'cpp')
endif
yaml_cpp_dep = dependency('yaml-cpp', required : true)
enable_libmagic = get_option('enable-libmagic')
magic_dep = dependency('', required : false)
if not enable_libmagic.disabled()
  magic_lib = cc.find_library('magic', required : enable_libmagic.enabled())
  if magic_lib.found()
    magic_dep = declare_dependency(dependencies : [magic_lib], compile_args : ['-DHAS_LIBMAGIC'])
  endif
endif

lexbor_dep = dependency('lexbor', required : false)
if lexbor_dep.found()
  add_project_arguments('-DHAS_LEXBOR', language : 'cpp')
endif

graphicsmagick_dep = dependency('GraphicsMagick++', required : false)
if graphicsmagick_dep.found()
  add_project_arguments('-DHAS_GRAPHICSMAGICK', language : 'cpp')
endif


lsp_fw = subproject('lsp-framework')
lsp_dep = lsp_fw.get_variable('lsp_dep')

# Define dependencies early
inc = include_directories('.', 'include', 'src')
ncurses = ncurses_dep

# Provide a libunwind dependency.
cc_c = meson.get_compiler('c')
libunwind_dep = cc_c.find_library('unwind', required: true)
add_project_link_arguments('-lunwind', language : ['cpp', 'c'])
dl_dep = cc_c.find_library('dl', required: true)

zydis_found = false
zydis_dep = dependency('zydis', required: false)
if zydis_dep.found()
  zydis_found = true
else
  zydis_lib = cc.find_library('Zydis', required: false)
  if zydis_lib.found() and cc.has_header('Zydis/Zydis.h')
    zydis_dep = declare_dependency(dependencies : zydis_lib)
    zydis_found = true
  else
    zydis_dep = declare_dependency()
  endif
endif

if zydis_found
  add_project_arguments('-DHAVE_ZYDIS=1', language : 'cpp')
endif

libdir = get_option('libdir')
prefix = get_option('prefix')

# Ensure we have the full, absolute path
if libdir.startswith('/')
  plugin_dir = libdir / 'turbostar' / 'plugins'
else
  plugin_dir = prefix / libdir / 'turbostar' / 'plugins'
endif

libexecdir = get_option('libexecdir')
if libexecdir.startswith('/')
  turbocatch_dir = libexecdir / 'turbostar'
else
  turbocatch_dir = prefix / libexecdir / 'turbostar'
endif

libturbocatch = shared_library('turbocatch',
  ['src/crash_catcher/crash_catcher.c', 'src/crash_catcher/perf_catcher.c'],
  dependencies: [libunwind_dep, dl_dep],
  override_options: ['b_sanitize=none'],
  install: true,
  install_dir: turbocatch_dir
)

subdir('src')

turbostar_exe = executable('turbostar',
  ['src/main.cpp', 'src/crash_handler.cpp', git_version_h],
  dependencies : [ui_dep, agent_dep, tools_dep, core_dep, ncurses_dep, lsp_dep, httplib_dep, nlohmann_json_dep, thread_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true,
  install : true)

turbostar_crashprocess_exe = executable('turbostar-crashprocess',
  ['src/crash_process.cpp', 'src/address_lookup.cpp'],
  dependencies : [nlohmann_json_dep, thread_dep],
  link_with: libturbostar_core,
  include_directories: inc,
  install : true)


install_symlink('turboagent',
  install_dir: 'bin',
  pointing_to: 'turbostar'
)

install_symlink('turboserver',
  install_dir: 'bin',
  pointing_to: 'turbostar'
)

install_symlink('turbomcp',
  install_dir: 'bin',
  pointing_to: 'turbostar'
)

agentcli_exe = executable('agentcli',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  export_dynamic: true,
  install : false)

agentcli_record_exe = executable('agentcli_record',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_RECORD'],
  export_dynamic: true,
  install : false)

agentcli_replay_exe = executable('agentcli_replay',
  'src/agentcli/main.cpp',
  dependencies : [agent_dep, tools_dep, core_dep, httplib_dep, nlohmann_json_dep, thread_dep, lsp_dep, re2_dep, sqlite3_dep, dl_dep],
  include_directories: inc,
  cpp_args: ['-DLLM_TRANSPORT_REPLAY'],
  export_dynamic: true,
  install : false)

turbostar_semcode_indexer_exe = executable('turbostar-semcode-indexer',
  'src/semcode_indexer_main.cpp',
  dependencies : [core_dep, sqlite3_dep, thread_dep],
  include_directories: inc,
  install : true)



if get_option('enable-tests')
  subdir('tests')
endif
