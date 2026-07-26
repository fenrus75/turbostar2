project('@@PROJECT_NAME_LOWER@@', 'c',
  version : '0.1.0',
  default_options : ['warning_level=3', 'c_std=@@LANGUAGE_STD@@'])

cc = meson.get_compiler('c')
foreach flag : ['-ftrivial-auto-var-init=zero', '-fzero-call-used-regs=used-gpr']
  if cc.has_argument(flag)
    add_project_arguments(flag, language : 'c')
  endif
endforeach

executable('@@EXECUTABLE_NAME@@',
  'src/main.c',
  install : true)
