project('turbostar', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++23'])

cc = meson.get_compiler('cpp')
ncurses_dep = dependency('ncursesw', required : false)
if not ncurses_dep.found()
  ncurses_dep = cc.find_library('ncursesw', required : true)
endif


subdir('src')

executable('turbostar',
  turbostar_sources,
  dependencies : [ncurses_dep],
  install : true)

