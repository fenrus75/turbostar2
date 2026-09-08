PROJECT ?= @@PROJECT_NAME_LOWER@@
VERILATOR ?= verilator
VERILATOR_FLAGS ?= -Wall -sv --lint-only

SRCS = src/top.sv src/top_tb.sv

.PHONY: all lint test clean

all: lint

lint:
	$(VERILATOR) $(VERILATOR_FLAGS) src/top.sv

test:
	@echo "Running lint checks as test..."
	$(VERILATOR) $(VERILATOR_FLAGS) src/top.sv src/top_tb.sv
	@echo "All tests passed."

clean:
	rm -rf obj_dir
