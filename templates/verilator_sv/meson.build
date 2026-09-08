project('@@PROJECT_NAME_LOWER@@', version : '0.1')

verilator = find_program('verilator', required : true)

srcs = files('src/top.sv')
tb_srcs = files('src/top.sv', 'src/top_tb.sv')

# Verilator lint-only compilation target for `fs_compile_project`
lint = custom_target('lint',
  input : srcs,
  output : 'lint.stamp',
  command : [verilator, '-Wall', '-sv', '--lint-only', '@INPUT@'],
  build_by_default : true
)

# Test targets for `fs_run_tests`
test('lint', verilator,
  args : ['-Wall', '-sv', '--lint-only', srcs],
  workdir : meson.project_source_root()
)

test('testbench', verilator,
  args : ['-Wall', '-sv', '--lint-only', tb_srcs],
  workdir : meson.project_source_root()
)
