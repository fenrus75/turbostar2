project('@@PROJECT_NAME_LOWER@@', version : '0.1')

yosys = find_program('yosys', required : true)
nextpnr = find_program('nextpnr-ice40', required : true)
icepack = find_program('icepack', required : true)
iceprog = find_program('iceprog', required : false)
verilator = find_program('verilator', required : false)

srcs = files('src/top.sv')
tb_srcs = files('src/top.sv', 'src/top_tb.sv')
pcf = files('pins.pcf')

# Verification & lint tests for `fs_run_tests`
if verilator.found()
  test('lint', verilator,
    args : ['-Wall', '-sv', '--lint-only', srcs],
    workdir : meson.project_source_root()
  )
  test('testbench', verilator,
    args : ['-Wall', '-sv', '--lint-only', tb_srcs],
    workdir : meson.project_source_root()
  )
endif

# Synthesis: SystemVerilog -> JSON netlist
synth_json = custom_target('top.json',
  input : srcs,
  output : 'top.json',
  command : [yosys, '-p', 'synth_ice40 -top top -json @OUTPUT@', '@INPUT@']
)

# Place & Route: nextpnr-ice40
asc = custom_target('top.asc',
  input : [synth_json],
  output : 'top.asc',
  depend_files : pcf,
  command : [nextpnr, '--hx8k', '--package', 'cb132', '--json', '@INPUT0@', '--pcf', pcf, '--asc', '@OUTPUT@']
)

# Bitstream packaging
bin = custom_target('top.bin',
  input : asc,
  output : 'top.bin',
  command : [icepack, '@INPUT@', '@OUTPUT@'],
  build_by_default : true
)

# Hardware flashing: meson compile -C build flash
if iceprog.found()
  run_target('flash',
    command : [iceprog, bin]
  )
endif
