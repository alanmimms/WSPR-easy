# Makefile for WSPR-ease FPGA Simulation
# Integrates Verilator with ESP32 SW code

# Directories
FPGA_DIR := .
SW_DIR := ../sw
SIM_DIR := sim
OBJDIR := build

# Verilator configuration
VERILATOR := verilator
VERILATOR_FLAGS := -Wall -Wno-fatal --trace --cc --exe --build
VERILATOR_FLAGS += -I$(SIM_DIR)
VERILATOR_FLAGS += -CFLAGS "-std=c++17 -I$(SW_DIR)"
VERILATOR_FLAGS += -LDFLAGS "-lm"

# Source files
RTL_SOURCES := top.sv sequencer.sv
RTL_SIM_SOURCES += $(SIM_DIR)/sbIO.sv $(SIM_DIR)/sbPLL40Core.sv

CPP_SOURCES := tbTop.cpp
SW_SOURCES := $(SW_DIR)/transmitter.cpp $(SW_DIR)/wspr-encoder.cpp

PROJECT=wspr_ease
TOP=Top
PCF=pins.pcf
DEVICE=up5k
PACKAGE=sg48

# Output image and sim executable
TARGET := $(OBJDIR)/fpga.img
SIMTARGET := $(OBJDIR)/VTop

.PHONY: all clean run sim fast superfast help

all: $(TARGET)

# The FPGA image
$(TARGET):	$(RTL_SOURCES)
	echo "--- Building FPGA Bitstream ---"
	mkdir -p $(OBJDIR)
	yosys -p "read_verilog -sv $(RTL_SOURCES); synth_ice40 -top $(TOP) -json $(PROJECT).json"
	nextpnr-ice40 --$(DEVICE) --package $(PACKAGE) \
		--json $(PROJECT).json --pcf $(PCF) --asc $(PROJECT).asc
	icepack $(PROJECT).asc $(PROJECT).bin
	mv -f $(PROJECT).bin $(TARGET)

# Build the Verilator simulation
$(SIMTARGET): $(RTL_SOURCES) $(RTL_SIM_SOURCES) $(CPP_SOURCES) $(SW_SOURCES) simHAL.hpp
	@echo "Building Verilator simulation with integrated SW code..."
	mkdir -p $(OBJDIR)
	$(VERILATOR) $(VERILATOR_FLAGS) \
		-o $(notdir $(SIMTARGET)) \
		--top-module Top \
		$(RTL_SOURCES) \
		$(RTL_SIM_SOURCES) \
		$(CPP_SOURCES) \
		$(SW_SOURCES)
	@echo "Build complete: $@"

# Run the simulation
run: $(SIMTARGET)
	@echo "Running simulation..."
	@cd $(OBJDIR) && ./VTop
	@echo "Simulation complete. Waveform saved to $(OBJDIR)/waveform.vcd"

# Alias for run
sim: run

# Run fast simulation (no waveforms)
fast: $(SIMTARGET)
	@echo "Running fast simulation (no waveform trace)..."
	@cd $(OBJDIR) && ./VTop --notrace
	@echo "Fast simulation complete."

# Run superfast simulation (event-driven, no waveforms)
superfast: $(SIMTARGET)
	@echo "Running superfast simulation (event-driven fast-forward)..."
	@cd $(OBJDIR) && ./VTop --notrace --fastforward
	@echo "Superfast simulation complete."

# View waveform with gtkwave (if available)
wave: $(OBJDIR)/waveform.vcd
	@if command -v gtkwave > /dev/null; then \
		gtkwave $< & \
	else \
		echo "gtkwave not found. Please install it to view waveforms."; \
	fi

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(OBJDIR)
	@rm -f waveform.vcd
	@echo "Clean complete."

# Help target
help:
	@echo "WSPR-ease FPGA Simulation Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build the simulation (default)"
	@echo "  run       - Build and run simulation with waveform tracing (~50s)"
	@echo "  fast      - Build and run fast simulation without waveforms (~7.6s)"
	@echo "  superfast - Build and run with event-driven fast-forward (~0.1s)"
	@echo "  sim       - Alias for 'run'"
	@echo "  wave      - Open waveform viewer (gtkwave)"
	@echo "  clean     - Remove all build artifacts"
	@echo "  help      - Show this help message"
	@echo ""
	@echo "The simulation integrates:"
	@echo "  - FPGA RTL (top.sv, sequencer.sv)"
	@echo "  - ESP32 SW code (transmitter.cpp, wspr-encoder.cpp)"
	@echo "  - Simulation HAL (simHAL.hpp)"
