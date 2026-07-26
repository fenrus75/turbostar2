project('@@PROJECT_NAME_LOWER@@', 'cpp',
  version : '0.1.0',
  default_options : ['warning_level=3', 'cpp_std=@@LANGUAGE_STD@@'])

cc = meson.get_compiler('cpp')
foreach flag : ['-ftrivial-auto-var-init=zero', '-fzero-call-used-regs=used-gpr']
  if cc.has_argument(flag)
    add_project_arguments(flag, language : 'cpp')
  endif
endforeach

executable('@@EXECUTABLE_NAME@@',
  'src/main.cpp',
  install : true)
