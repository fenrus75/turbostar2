PROJECT ?= @@PROJECT_NAME_LOWER@@
TOP ?= top
PCF ?= pins.pcf
DEVICE ?= --hx8k
PACKAGE ?= cb132

YOSYS ?= yosys
NEXTPNR ?= nextpnr-ice40
ICEPACK ?= icepack
ICEPROG ?= iceprog
VERILATOR ?= verilator
VERILATOR_FLAGS ?= -Wall -sv --lint-only

SRCS = src/top.sv
TB_SRCS = src/top.sv src/top_tb.sv

SYNTH_DIR = synth
JSON = $(SYNTH_DIR)/$(TOP).json
ASC  = $(SYNTH_DIR)/$(TOP).asc
BIN  = $(SYNTH_DIR)/$(TOP).bin

.PHONY: all lint test synth pnr bitstream flash program clean

all: bitstream

lint:
	$(VERILATOR) $(VERILATOR_FLAGS) $(SRCS)

test:
	@echo "Running lint and testbench verification..."
	$(VERILATOR) $(VERILATOR_FLAGS) $(TB_SRCS)
	@echo "All tests passed."

$(SYNTH_DIR):
	mkdir -p $(SYNTH_DIR)

$(JSON): $(SRCS) | $(SYNTH_DIR)
	$(YOSYS) -p 'synth_ice40 -top $(TOP) -json $(JSON)' $(SRCS)

synth: $(JSON)

$(ASC): $(JSON) $(PCF)
	$(NEXTPNR) $(DEVICE) --package $(PACKAGE) --json $(JSON) --pcf $(PCF) --asc $(ASC)

pnr: $(ASC)

$(BIN): $(ASC)
	$(ICEPACK) $(ASC) $(BIN)

bitstream: $(BIN)

flash: $(BIN)
	$(ICEPROG) $(BIN)

program: flash

clean:
	rm -rf $(SYNTH_DIR) obj_dir *.vcd
